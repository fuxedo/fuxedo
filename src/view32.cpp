// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include "view32.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <list>
#include <mutex>
#include <set>
#include <unordered_map>

#include "fbfr32.h"
#include "misc.h"

namespace fux::view {

static int32_t alignof_type(int type) {
  switch (type) {
    case FLD_SHORT:
      return 2;
    case FLD_LONG:
    case FLD_DOUBLE:
      return 8;
    case FLD_FLOAT:
      return 4;
    default:
      return 1;
  }
}

static int32_t elsize_of(int type, int32_t size) {
  switch (type) {
    case FLD_SHORT:
      return 2;
    case FLD_LONG:
      return 8;
    case FLD_CHAR:
      return 1;
    case FLD_FLOAT:
      return 4;
    case FLD_DOUBLE:
      return 8;
    case FLD_STRING:
    case FLD_CARRAY:
      return size;
    default:
      return 0;
  }
}

std::vector<viewdef> viewfile_parser::parse() {
  viewdef *cur = nullptr;
  while (parse_line(cur))
    ;
  if (sym_ != EOF) {
    throw basic_parser_error("unrecognized input", row_, "");
  }
  if (cur != nullptr) {
    throw basic_parser_error("missing END", row_, "VIEW " + cur->name);
  }
  return std::move(views_);
}

bool viewfile_parser::parse_line(viewdef *&cur) {
  if (accept('\n')) {
    return true;
  }
  if (accept('#')) {
    rest_of_line();
  } else if (accept('$')) {
    if (cur == nullptr) {
      throw basic_parser_error("No view definition", row_, "VIEW\tVIEWNAME");
    }
    std::string raw;
    rest_of_line(&raw);
    cur->code.push_back(raw);
  } else {
    std::string first;
    if (name(&first)) {
      if (first == "VIEW") {
        if (cur != nullptr) {
          throw basic_parser_error("missing END", row_, "VIEW " + cur->name);
        }
        std::string vname;
        if (!delim() || !name(&vname)) {
          throw basic_parser_error("View name expected", row_,
                                   "VIEW\tVIEWNAME");
        }
        views_.push_back(viewdef{vname, {}, {}, 0});
        cur = &views_.back();
      } else if (first == "END") {
        if (cur == nullptr) {
          throw basic_parser_error("No view definition", row_,
                                   "VIEW\tVIEWNAME\nEND");
        }
        cur = nullptr;
      } else {
        if (cur == nullptr) {
          throw basic_parser_error("No view definition", row_,
                                   "VIEW\tVIEWNAME");
        }
        member m;
        m.type = -1;
        std::string type = first, count, flag;
        std::string size, nullv;

        bool read = delim() && name(&m.cname) && delim() &&
                    opt_field(&m.fbname) && delim() &&
                    unsigned_number(&count) && delim() && opt_flags(&flag) &&
                    delim() && opt_number(&size);
        // the null value may be omitted, defaulting to the member type NULL
        if (read && delim()) {
          rest_of_line(&nullv);
        }
        if (!read) {
          throw basic_parser_error("unrecognized input", row_,
                                   "type cname fbname count flag size null");
        }
        if (!nullv.empty() && nullv.back() == '\r') {
          nullv.pop_back();
        }

        static const std::map<std::string, int> types = {
            {"char", FLD_CHAR},   {"short", FLD_SHORT}, {"int", FLD_LONG},
            {"long", FLD_LONG},   {"float", FLD_FLOAT}, {"double", FLD_DOUBLE},
            {"string", FLD_STRING}, {"carray", FLD_CARRAY},
        };
        if (type == "-") {
          m.type = -1;  // resolved from fbname later
        } else {
          auto it = types.find(type);
          if (it == types.end()) {
            throw basic_parser_error("unsupported member type", row_, type);
          }
          m.type = it->second;
        }

        m.count = std::stol(count);
        if (m.count < 1) {
          throw basic_parser_error("count must be positive", row_, count);
        }

        // C, L and P are orthogonal to the direction flags; the absence of
        // F, S and N means a two-way mapping
        bool has_f = false, has_s = false, has_n = false;
        if (!flag.empty()) {
          for (auto c : flag) {
            if (c == 'C') {
              m.flags |= F_COUNT;
            } else if (c == 'L') {
              m.flags |= F_LENGTH;
            } else if (c == 'P') {
              m.flags |= F_PROP;
            } else if (c == 'F') {
              has_f = true;
            } else if (c == 'S') {
              has_s = true;
            } else if (c == 'N') {
              has_n = true;
            } else {
              throw basic_parser_error("unknown flag", row_,
                                       std::string(1, c));
            }
          }
          m.flags &= ~(F_STOF | F_FTOS);
          if (has_n) {
            // zero-way mapping
          } else if (has_f && !has_s) {
            m.flags |= F_STOF;
          } else if (has_s && !has_f) {
            m.flags |= F_FTOS;
          } else {
            m.flags |= F_STOF | F_FTOS;
          }
        }

        if (!size.empty()) {
          if (m.type != FLD_STRING && m.type != FLD_CARRAY) {
            throw basic_parser_error(
                "size supported only for string and carray", row_, size);
          }
          m.elsize = std::stol(size);
          if (m.elsize < 1) {
            throw basic_parser_error("size must be positive", row_, size);
          }
        }

        // null value: "-", NONE, quoted, or a bare token
        if (nullv == "-") {
          // default, resolved once the type is known
        } else if (nullv == "NONE") {
          m.null_none = true;
        } else if (nullv.size() >= 2 &&
                   (nullv.front() == '\'' || nullv.front() == '"') &&
                   nullv.back() == nullv.front()) {
          std::string unquoted;
          size_t i = 1;
          for (; i + 1 < nullv.size(); i++) {
            char c = nullv[i];
            if (c == '\'' || c == '"') {
              throw basic_parser_error("unescaped quote in null value", row_,
                                       nullv);
            }
            if (c != '\\') {
              unquoted.push_back(c);
              continue;
            }
            char e = nullv[++i];
            switch (e) {
              case '0':
              case '1':
              case '2':
              case '3':
              case '4':
              case '5':
              case '6':
              case '7': {
                int oct = e - '0';
                for (int n = 0; n < 2 && i + 1 < nullv.size() - 1 &&
                                 nullv[i + 1] >= '0' && nullv[i + 1] <= '7';
                     n++) {
                  oct = oct * 8 + (nullv[++i] - '0');
                }
                unquoted.push_back(static_cast<char>(oct));
                break;
              }
              case 'n':
                unquoted.push_back('\n');
                break;
              case 't':
                unquoted.push_back('\t');
                break;
              case 'v':
                unquoted.push_back('\v');
                break;
              case 'b':
                unquoted.push_back('\b');
                break;
              case 'r':
                unquoted.push_back('\r');
                break;
              case 'f':
                unquoted.push_back('\f');
                break;
              case '\\':
                unquoted.push_back('\\');
                break;
              case '\'':
                unquoted.push_back('\'');
                break;
              case '"':
                unquoted.push_back('"');
                break;
              default:
                throw basic_parser_error("bad escape in null value", row_,
                                         nullv);
            }
          }
          if (i + 1 != nullv.size()) {
            throw basic_parser_error("unescaped quote in null value", row_,
                                     nullv);
          }
          m.nullval.assign(unquoted.begin(), unquoted.end());
        } else {
          m.nullval.assign(nullv.begin(), nullv.end());
        }

        cur->members.push_back(std::move(m));
      }
    } else {
      accept([](int c) { return ::isspace(c) && c != '\n'; });
    }
  }
  return accept('\n');
}

static void set_bytes(std::vector<char> &v, const void *data, size_t len) {
  v.resize(len);
  std::memcpy(v.data(), data, len);
}

static bool is_string_type(int type) {
  return type == FLD_STRING || type == FLD_CARRAY;
}

bool resolve(std::vector<viewdef> &views, bool no_fml,
             std::vector<std::string> &errors,
             std::vector<std::string> &warnings) {
  bool ok = true;
  for (auto &v : views) {
    std::map<std::string, int> cnames;
    std::map<FLDID32, std::string> fldids;
    for (auto &m : v.members) {
      if (cnames.count(m.cname) != 0) {
        errors.push_back("view " + v.name + ": duplicate member name " +
                         m.cname);
        ok = false;
      }
      cnames[m.cname] = 1;

      if (!no_fml && !m.fbname.empty()) {
        m.fldid = Fldid32(const_cast<char *>(m.fbname.c_str()));
        if (m.fldid == BADFLDID) {
          warnings.push_back("view " + v.name + ": field " + m.fbname +
                             " not found, member " + m.cname + " not mapped");
        }
      }
      if (fldids.count(m.fldid) != 0) {
        errors.push_back("view " + v.name + ": field " +
                         (m.fbname.empty() ? std::string("-") : m.fbname) +
                         " mapped more than once");
        ok = false;
      }
      fldids[m.fldid] = m.cname;

      if (m.type == -1) {
        if (m.fldid == BADFLDID) {
          errors.push_back("view " + v.name + ": member " + m.cname +
                           " has no type and no field");
          ok = false;
          m.type = FLD_LONG;
        } else {
          m.type = Fldtype32(m.fldid);
          if (m.type == FLD_FML32 || m.type == FLD_PTR ||
              m.type == FLD_VIEW32 || m.type == FLD_MBSTRING) {
            errors.push_back("view " + v.name + ": member " + m.cname +
                             " has unsupported field type");
            ok = false;
          }
        }
      }
      if (no_fml) {
        // mapping flags do not apply to independent views
        m.flags &= ~(F_STOF | F_FTOS);
        m.fldid = 0;
      }

      if (is_string_type(m.type)) {
        if (m.elsize == 0) {
          // Oracle defaults a missing size to 2 bytes
          warnings.push_back("view " + v.name + ": member " + m.cname +
                             " has no size, defaulting to 2");
          m.elsize = 2;
        }
      } else {
        m.elsize = elsize_of(m.type, 0);
      }
      if ((m.flags & F_LENGTH) && !is_string_type(m.type)) {
        errors.push_back("view " + v.name + ": member " + m.cname +
                         " can not have the L flag");
        ok = false;
        m.flags &= ~F_LENGTH;
      }

      // NULL value
      if (m.null_none) {
        // no value is NULL for this member
      } else if (m.type == FLD_CHAR) {
        if (m.nullval.empty()) {
          m.nullval.push_back(0);
        } else if (m.nullval.size() > 1) {
          m.nullval.resize(1);
        }
      } else if (!is_string_type(m.type)) {
        if (m.nullval.empty()) {
          long zero = 0;
          set_bytes(m.nullval, &zero, sizeof(zero));
        } else {
          // normalize numeric NULL values to the member size
          std::string s(m.nullval.begin(), m.nullval.end());
          if (m.type == FLD_FLOAT || m.type == FLD_DOUBLE) {
            double d = std::stod(s);
            if (m.type == FLD_FLOAT) {
              float f = static_cast<float>(d);
              set_bytes(m.nullval, &f, sizeof(f));
            } else {
              set_bytes(m.nullval, &d, sizeof(d));
            }
          } else {
            long l = std::stol(s);
            if (m.type == FLD_SHORT) {
              short f = static_cast<short>(l);
              set_bytes(m.nullval, &f, sizeof(f));
            } else {
              set_bytes(m.nullval, &l, sizeof(l));
            }
          }
        }
      }
    }

    for (auto &m : v.members) {
      // NULL fill patterns mirror what sinit writes into the member
      auto make_fill = [&](bool prop) {
        std::vector<char> fill(m.elsize, 0);
        auto n = std::min<int32_t>(m.nullval.size(), m.elsize);
        std::copy_n(m.nullval.begin(), n, fill.begin());
        if (prop && n > 0) {
          if (m.type == FLD_STRING) {
            std::fill(fill.begin() + n, fill.end() - 1, m.nullval.back());
          } else {
            std::fill(fill.begin() + n, fill.end(), m.nullval.back());
          }
        }
        return fill;
      };
      m.fill = make_fill((m.flags & F_PROP) != 0);
      m.fill_nop = make_fill(false);
    }

    std::set<std::string> generated;
    for (auto &m : v.members) {
      if (m.flags & F_COUNT) {
        generated.insert("C_" + m.cname);
      }
      if (m.flags & F_LENGTH) {
        generated.insert("L_" + m.cname);
      }
    }
    for (auto &m : v.members) {
      if (generated.count(m.cname) != 0) {
        errors.push_back("view " + v.name + ": member name " + m.cname +
                         " conflicts with a generated count or length name");
        ok = false;
      }
    }
  }
  return ok;
}

static int32_t align_to(int32_t off, int32_t alignment) {
  return (off + alignment - 1) / alignment * alignment;
}

void layout(viewdef &view) {
  int32_t off = 0;
  int32_t max_align = 1;
  for (auto &m : view.members) {
    if (m.flags & F_LENGTH) {
      m.alm_off = align_to(off, 4);
      off = m.alm_off + 4 * m.count;
      max_align = std::max<int32_t>(max_align, 4);
    }
    if (m.flags & F_COUNT) {
      m.acm_off = align_to(off, 4);
      off = m.acm_off + 4;
      max_align = std::max<int32_t>(max_align, 4);
    }
    m.off = align_to(off, alignof_type(m.type));
    off = m.off + m.elsize * m.count;
    max_align = std::max<int32_t>(max_align, alignof_type(m.type));
  }
  view.vsize = align_to(off, max_align);
}

namespace {

void put(std::ostream &out, const void *data, size_t len) {
  out.write(reinterpret_cast<const char *>(data), len);
}

template <typename T>
void put(std::ostream &out, T v) {
  put(out, &v, sizeof(v));
}

void put(std::ostream &out, const std::string &s) {
  auto len = static_cast<uint32_t>(s.size());
  put(out, len);
  put(out, s.data(), s.size());
}

void put(std::ostream &out, const std::vector<char> &v) {
  auto len = static_cast<uint32_t>(v.size());
  put(out, len);
  if (len != 0) {
    put(out, v.data(), v.size());
  }
}

template <typename T>
T get(std::istream &in) {
  T v;
  in.read(reinterpret_cast<char *>(&v), sizeof(v));
  if (!in) {
    throw std::runtime_error("truncated viewfile");
  }
  return v;
}

void get(std::istream &in, void *data, size_t len) {
  in.read(reinterpret_cast<char *>(data), len);
  if (!in) {
    throw std::runtime_error("truncated viewfile");
  }
}

std::string get_string(std::istream &in) {
  auto len = get<uint32_t>(in);
  std::string s(len, '\0');
  if (len != 0) {
    get(in, &s[0], len);
  }
  return s;
}

std::vector<char> get_bytes(std::istream &in) {
  auto len = get<uint32_t>(in);
  std::vector<char> v(len);
  if (len != 0) {
    get(in, v.data(), len);
  }
  return v;
}

}  // namespace

void write_file(std::ostream &out, const std::vector<viewdef> &views) {
  const char magic[8] = {'F', 'U', 'X', 'V', '3', '2', '\0', '\1'};
  put(out, magic, sizeof(magic));
  put<uint32_t>(out, views.size());
  for (auto &v : views) {
    put(out, v.name);
    put<int32_t>(out, v.vsize);
    put<uint32_t>(out, v.code.size());
    for (auto &line : v.code) {
      put(out, line);
    }
    put<uint32_t>(out, v.members.size());
    for (auto &m : v.members) {
      put(out, m.cname);
      put(out, m.fbname);
      put<uint32_t>(out, m.fldid);
      put<int32_t>(out, m.type);
      put<int32_t>(out, m.count);
      put<int32_t>(out, m.flags);
      put<int32_t>(out, m.elsize);
      put(out, m.nullval);
      put(out, m.fill);
      put(out, m.fill_nop);
      put<int32_t>(out, m.null_none ? 1 : 0);
      put<int32_t>(out, m.off);
      put<int32_t>(out, m.acm_off);
      put<int32_t>(out, m.alm_off);
    }
  }
}

std::vector<viewdef> read_file(std::istream &in) {
  char magic[8];
  get(in, magic, sizeof(magic));
  if (memcmp(magic, "FUXV32", 6) != 0 || magic[7] != 1) {
    throw std::runtime_error("not a fuxedo viewfile");
  }
  std::vector<viewdef> views;
  auto nviews = get<uint32_t>(in);
  views.reserve(nviews);
  for (uint32_t i = 0; i < nviews; i++) {
    viewdef v;
    v.name = get_string(in);
    v.vsize = get<int32_t>(in);
    auto ncode = get<uint32_t>(in);
    for (uint32_t j = 0; j < ncode; j++) {
      v.code.push_back(get_string(in));
    }
    auto nmembers = get<uint32_t>(in);
    v.members.reserve(nmembers);
    for (uint32_t j = 0; j < nmembers; j++) {
      member m;
      m.cname = get_string(in);
      m.fbname = get_string(in);
      m.fldid = get<uint32_t>(in);
      m.type = get<int32_t>(in);
      m.count = get<int32_t>(in);
      m.flags = get<int32_t>(in);
      m.elsize = get<int32_t>(in);
      m.nullval = get_bytes(in);
      m.fill = get_bytes(in);
      m.fill_nop = get_bytes(in);
      m.null_none = get<int32_t>(in) != 0;
      if (m.fill.empty()) {
        m.fill.resize(m.elsize, 0);
        auto n = std::min<int32_t>(m.nullval.size(), m.elsize);
        std::copy_n(m.nullval.begin(), n, m.fill.begin());
        m.fill_nop = m.fill;
      }
      m.off = get<int32_t>(in);
      m.acm_off = get<int32_t>(in);
      m.alm_off = get<int32_t>(in);
      v.members.push_back(std::move(m));
    }
    views.push_back(std::move(v));
  }
  return views;
}

namespace {

class registry {
 public:
  std::shared_ptr<viewdef> find(const std::string &name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = views_.find(name);
    if (it != views_.end()) {
      lru_.splice(lru_.begin(), lru_, it->second.second);
      return it->second.first;
    }
    if (!load(name)) {
      return nullptr;
    }
    it = views_.find(name);
    if (it == views_.end()) {
      if (file_found_) {
        FERROR(FBADVIEW, "view %s not found", name.c_str());
      } else {
        FERROR(FVFOPEN, "no viewfile for view %s", name.c_str());
      }
      return nullptr;
    }
    lru_.splice(lru_.begin(), lru_, it->second.second);
    return it->second.first;
  }

  void refresh() {
    std::lock_guard<std::mutex> lock(mutex_);
    views_.clear();
    lru_.clear();
  }

 private:
  bool load(const std::string &wanted) {
    file_found_ = false;
    auto files = split(fux::util::getenv("VIEWFILES32", ""), ',');
    auto dirs = split(fux::util::getenv("VIEWDIR32", ""), ':');
    if (dirs.empty()) {
      dirs.push_back(".");
    }

    for (auto &file : files) {
      if (file.empty()) {
        continue;
      }
      std::vector<std::string> paths;
      if (file.find('/') != std::string::npos) {
        paths.push_back(file);
      } else {
        for (auto &dir : dirs) {
          paths.push_back((dir.empty() ? std::string(".") : dir) + "/" + file);
        }
      }
      for (auto &path : paths) {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
          continue;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
          FERROR(FVFOPEN, "can not open viewfile %s", path.c_str());
          return false;
        }
        file_found_ = true;
        std::vector<viewdef> views;
        try {
          views = read_file(in);
        } catch (const std::exception &) {
          FERROR(FVFSYNTAX, "bad viewfile %s", path.c_str());
          return false;
        }
        for (auto &v : views) {
          std::string vname = v.name;
          add(vname, std::make_shared<viewdef>(std::move(v)));
        }
        return true;
      }
    }
    return true;
  }

  void add(const std::string &name, std::shared_ptr<viewdef> view) {
    if (views_.count(name) != 0) {
      // first viewfile with the name wins
      return;
    }
    lru_.push_front(name);
    views_[name] = {view, lru_.begin()};
    while (views_.size() > FVIEWCACHESIZE) {
      views_.erase(lru_.back());
      lru_.pop_back();
    }
  }

  static std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
      auto pos = s.find(delim, start);
      if (pos == std::string::npos) {
        out.push_back(s.substr(start));
        break;
      }
      out.push_back(s.substr(start, pos - start));
      start = pos + 1;
    }
    return out;
  }

  std::mutex mutex_;
  bool file_found_ = false;
  std::unordered_map<std::string,
                     std::pair<std::shared_ptr<viewdef>,
                               std::list<std::string>::iterator>>
      views_;
  std::list<std::string> lru_;
};

registry g_registry;

}  // namespace

std::shared_ptr<viewdef> get(const std::string &name) {
  return g_registry.find(name);
}

void refresh() { g_registry.refresh(); }

const member *find_member(const viewdef &view, const std::string &cname) {
  for (auto &m : view.members) {
    if (m.cname == cname) {
      return &m;
    }
  }
  return nullptr;
}

static void set_null_el(char *cstruct, const member &m, int32_t i) {
  char *el = cstruct + m.off + i * m.elsize;
  std::copy_n(m.fill.data(), m.elsize, el);
}

static int32_t null_len(const member &m) {
  if (!is_string_type(m.type)) {
    return 0;
  }
  return m.nullval.size();
}

bool is_null(const member &m, const char *el) {
  if (m.null_none) {
    return false;
  }
  return memcmp(el, m.fill.data(), m.elsize) == 0;
}

// The Fvstof32() NULL test: the P flag does not propagate here and carray
// members with an ALM are never NULL
static bool stof_is_null(const member &m, const char *el) {
  if (m.null_none) {
    return false;
  }
  if (m.type == FLD_CARRAY && (m.flags & F_LENGTH)) {
    return false;
  }
  return memcmp(el, m.fill_nop.data(), m.elsize) == 0;
}

void sinit(char *cstruct, viewdef &view) {
  for (auto &m : view.members) {
    for (int32_t i = 0; i < m.count; i++) {
      set_null_el(cstruct, m, i);
    }
    if (m.flags & F_COUNT) {
      // Oracle clears only the low 16 bits of the count member
      *reinterpret_cast<int16_t *>(cstruct + m.acm_off) = 0;
    }
    if (m.flags & F_LENGTH) {
      auto *alm = reinterpret_cast<uint32_t *>(cstruct + m.alm_off);
      for (int32_t i = 0; i < m.count; i++) {
        alm[i] = null_len(m);
      }
    }
  }
}

void selinit(char *cstruct, member &m) {
  for (int32_t i = 0; i < m.count; i++) {
    set_null_el(cstruct, m, i);
  }
  if (m.flags & F_COUNT) {
    *reinterpret_cast<int16_t *>(cstruct + m.acm_off) = 0;
  }
  if (m.flags & F_LENGTH) {
    auto *alm = reinterpret_cast<uint32_t *>(cstruct + m.alm_off);
    for (int32_t i = 0; i < m.count; i++) {
      alm[i] = null_len(m);
    }
  }
}

int to_struct(Fbfr32 *fbfr, char *cstruct, viewdef &view) {
  for (auto &m : view.members) {
    int32_t n = 0;
    if ((m.flags & F_FTOS) && m.fldid != 0) {
      n = fbfr->occur(m.fldid);
    }
    int32_t take = std::min<int32_t>(n, m.count);
    for (int32_t i = 0; i < take; i++) {
      char *el = cstruct + m.off + i * m.elsize;
      FLDLEN32 flen = 0;
      char *val = CFfind32(fbfr, m.fldid, i, &flen, m.type);
      if (val == nullptr) {
        FERROR(FEINVAL, "can not convert field %s", m.fbname.c_str());
        return -1;
      }
      if (m.type == FLD_STRING) {
        memset(el, 0, m.elsize);
        // leave room for the terminator, like strlcpy
        auto n = std::min<FLDLEN32>(flen > 0 ? flen - 1 : 0, m.elsize - 1);
        std::copy_n(val, n, el);
      } else if (m.type == FLD_CARRAY) {
        auto n = std::min<FLDLEN32>(flen, m.elsize);
        std::copy_n(val, n, el);
      } else {
        std::copy_n(val, m.elsize, el);
      }
    }
    for (int32_t i = take; i < m.count; i++) {
      set_null_el(cstruct, m, i);
    }
    if (m.flags & F_COUNT) {
      auto *acm = reinterpret_cast<int32_t *>(cstruct + m.acm_off);
      *acm = n > m.count ? -(n - m.count) : take;
    }
    if (m.flags & F_LENGTH) {
      auto *alm = reinterpret_cast<uint32_t *>(cstruct + m.alm_off);
      for (int32_t i = 0; i < m.count; i++) {
        alm[i] = null_len(m);
      }
      for (int32_t i = 0; i < take; i++) {
        FLDLEN32 flen = 0;
        char *val = fbfr->find(m.fldid, i, &flen);
        if (val == nullptr) {
          continue;
        }
        alm[i] = flen;
      }
    }
  }
  return 0;
}

// Converts a member element to the field type, returning a pointer to a
// thread local buffer and the field length.
static char *to_field(const member &m, const char *el, FLDLEN32 *flen) {
  thread_local std::string buf;
  int ftype = Fldtype32(m.fldid);
  if (ftype == m.type) {
    if (m.type == FLD_STRING) {
      size_t n = strnlen(el, m.elsize);
      if (n < m.elsize) {
        n++;
      }
      buf.assign(el, n);
      *flen = n;
    } else if (m.type == FLD_CARRAY) {
      buf.assign(el, m.elsize);
      *flen = m.elsize;
    } else {
      buf.assign(el, m.elsize);
      *flen = m.elsize;
    }
    return const_cast<char *>(buf.data());
  }
  FLDLEN32 fromlen = m.elsize;
  if (m.type == FLD_STRING) {
    fromlen = strnlen(el, m.elsize) + 1;
  }
  return Ftypcvt32(flen, ftype, const_cast<char *>(el), m.type, fromlen);
}

int from_struct(Fbfr32 *fbfr, char *cstruct, int mode, viewdef &view) {
  if (mode != FUPDATE && mode != FJOIN && mode != FOJOIN && mode != FCONCAT) {
    FERROR(FEINVAL, "invalid mode %d", mode);
    return -1;
  }

  auto *tmp = Falloc32(16, 2048);
  if (tmp == nullptr) {
    return -1;
  }

  int rc = 0;
  for (auto &m : view.members) {
    if (!(m.flags & F_STOF) || m.fldid == 0) {
      continue;
    }
    int32_t n = m.count;
    if (m.flags & F_COUNT) {
      auto acm = *reinterpret_cast<int32_t *>(cstruct + m.acm_off);
      if (acm < 0) {
        FERROR(FBADACM, "member %s has a negative count", m.cname.c_str());
        rc = -1;
        goto done;
      }
      n = std::min<int32_t>(acm, m.count);
    }
    int32_t transferred = 0;
    for (int32_t i = 0; i < n; i++) {
      char *el = cstruct + m.off + i * m.elsize;
      if (stof_is_null(m, el)) {
        continue;
      }
      FLDLEN32 flen = 0;
      char *fval = to_field(m, el, &flen);
      if (fval == nullptr) {
        rc = -1;
        goto done;
      }
      if (m.type == FLD_CARRAY && (m.flags & F_LENGTH)) {
        auto alm = reinterpret_cast<uint32_t *>(cstruct + m.alm_off)[i];
        flen = std::min<uint32_t>(alm, m.elsize);
        fval = el;
      }
      while (true) {
        int ar = Fadd32(tmp, m.fldid, fval, flen);
        if (ar != -1) {
          break;
        }
        if (Ferror32 != FNOSPACE) {
          rc = -1;
          goto done;
        }
        auto b = reinterpret_cast<Fbfr32 *>(tmp);
        if (Frealloc32(tmp, 16, b->size() * 2) == nullptr) {
          rc = -1;
          goto done;
        }
      }
      transferred++;
    }
    if (m.flags & F_COUNT) {
      *reinterpret_cast<int32_t *>(cstruct + m.acm_off) = transferred;
    }
  }

  if (mode == FUPDATE) {
    rc = fbfr->update(tmp);
  } else if (mode == FJOIN) {
    rc = fbfr->join(tmp);
  } else if (mode == FOJOIN) {
    rc = fbfr->ojoin(tmp);
  } else {
    rc = fbfr->concat(tmp);
  }

done:
  // Ffree32() resets Ferror32 through its own exception boundary, keep the
  // error the conversion reported
  {
    int err = Ferror32;
    Ffree32(tmp);
    if (err != 0) {
      Ferror32 = err;
    }
  }
  return rc;
}

}  // namespace fux::view
