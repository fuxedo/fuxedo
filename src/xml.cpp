// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// XML <-> FML32 conversion behind tpxmltofml32()/tpfml32toxml() and the
// *SERVICES BUFTYPECONV=XML2FML32 boundary conversion. Semantics pinned
// against Oracle Tuxedo 12.2 (probe session, private/compat/xml):
// element names map through Fldid32(), repeated elements become field
// occurrences, an element whose field is FLD_FML32 becomes a nested
// buffer, unknown element names set Ferror32=FBADNAME and are skipped,
// the document element itself is not added as a field, attributes are
// ignored, and character data is kept verbatim. tpfml32toxml emits a
// declaration, a literal <FML32 Type="FML32"> root, and one element per
// field in buffer order.

#include "xml.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <atmi.h>
#include <fml32.h>
#include "misc.h"
#include "xml.h"

namespace fux::xml {

namespace {

// Minimal well-formed-XML reader in the same combinator style as
// basic_parser, but over an in-memory string: the grammar needs 2-4
// character lookahead, which a streambuf cannot rewind.
struct xmlparser {
  explicit xmlparser(std::string in) : in_(std::move(in)) {}

  struct element {
    std::string name;
    std::string text;  // concatenated character data
    std::vector<element> children;
  };

  element parse_document() {
    skip_misc();
    element root = parse_element();
    skip_misc();
    if (!eof()) {
      throw std::runtime_error("trailing content");
    }
    return root;
  }

  bool eof() const { return pos_ >= in_.size(); }
  int sym() const { return eof() ? -1 : static_cast<unsigned char>(in_[pos_]); }

  bool accept(int ch) {
    if (sym() == ch) {
      ++pos_;
      return true;
    }
    return false;
  }

  bool seq(const char *lit) {  // expect and consume, throw on mismatch
    for (const char *p = lit; *p != '\0'; ++p) {
      if (!accept(static_cast<unsigned char>(*p))) {
        throw std::runtime_error(std::string("expected ") + lit);
      }
    }
    return true;
  }

  bool blank() {
    bool any = false;
    while (!eof() && (sym() == ' ' || sym() == '\t' || sym() == '\n' ||
                      sym() == '\r')) {
      ++pos_;
      any = true;
    }
    return any;
  }

  bool name(std::string *out) {
    if (eof() || !(isalpha(sym()) || sym() == '_' || sym() == ':')) {
      return false;
    }
    while (!eof() &&
           (isalnum(sym()) || sym() == '_' || sym() == '-' || sym() == ':' ||
            sym() == '.')) {
      *out += static_cast<char>(sym());
      ++pos_;
    }
    return true;
  }

  void scan_until(const char *lit) {
    size_t found = in_.find(lit, pos_);
    if (found == std::string::npos) {
      throw std::runtime_error(std::string("unterminated ") + lit);
    }
    pos_ = found + strlen(lit);
  }

  void skip_misc() {
    while (true) {
      blank();
      if (eof() || sym() != '<') {
        return;
      }
      if (in_.compare(pos_, 4, "<!--") == 0) {
        pos_ += 4;
        scan_until("-->");
      } else if (in_.compare(pos_, 2, "<?") == 0) {
        pos_ += 2;
        scan_until("?>");
      } else if (in_.compare(pos_, 2, "<!") == 0) {
        pos_ += 2;
        scan_until(">");
      } else {
        return;
      }
    }
  }

  element parse_element() {
    seq("<");
    element e;
    if (!name(&e.name)) {
      throw std::runtime_error("element name expected");
    }
    // attributes are read and dropped
    while (true) {
      blank();
      if (accept('/')) {
        seq(">");
        return e;
      }
      if (accept('>')) {
        break;
      }
      std::string attr;
      if (!name(&attr)) {
        throw std::runtime_error("attribute expected");
      }
      blank();
      if (accept('=')) {
        blank();
        if (accept('"')) {
          size_t end = in_.find('"', pos_);
          if (end == std::string::npos) {
            throw std::runtime_error("unterminated attribute");
          }
          pos_ = end + 1;
        } else if (accept('\'')) {
          size_t end = in_.find('\'', pos_);
          if (end == std::string::npos) {
            throw std::runtime_error("unterminated attribute");
          }
          pos_ = end + 1;
        } else {
          throw std::runtime_error("attribute value expected");
        }
      }
    }
    while (true) {
      if (eof()) {
        throw std::runtime_error("unterminated element " + e.name);
      }
      if (sym() == '<') {
        if (in_.compare(pos_, 4, "<!--") == 0 ||
            in_.compare(pos_, 2, "<?") == 0 ||
            (in_.compare(pos_, 2, "<!") == 0 &&
             in_.compare(pos_, 4, "<!--") != 0)) {
          skip_misc();
        } else if (in_.compare(pos_, 2, "</") == 0) {
          pos_ += 2;
          std::string close;
          if (!name(&close) || close != e.name) {
            throw std::runtime_error("mismatched close tag for " + e.name);
          }
          blank();
          seq(">");
          return e;
        } else {
          e.children.push_back(parse_element());
        }
      } else {
        e.text += static_cast<char>(sym());
        ++pos_;
      }
    }
  }

  std::string in_;
  size_t pos_ = 0;
};

using element = xmlparser::element;

void grow(FBFR32 *&f) {
  auto grown = Frealloc32(f, 16, Fsizeof32(f) * 2);
  if (grown == nullptr) {
    throw std::runtime_error("out of memory");
  }
  f = grown;
}

int add_typed(FBFR32 *f, FLDID32 fid, const std::string &text) {
  int type = Fldtype32(fid);
  switch (type) {
    case FLD_STRING:
    case FLD_CARRAY:
      return Fadd32(f, fid, const_cast<char *>(text.c_str()),
                    static_cast<FLDLEN32>(text.size()));
    case FLD_CHAR: {
      char c = text.empty() ? '\0' : text[0];
      return Fadd32(f, fid, &c, 0);
    }
    case FLD_SHORT: {
      short v = text.empty() ? 0 : static_cast<short>(std::stol(text));
      return Fadd32(f, fid, reinterpret_cast<char *>(&v), 0);
    }
    case FLD_LONG: {
      long v = text.empty() ? 0 : std::stol(text);
      return Fadd32(f, fid, reinterpret_cast<char *>(&v), 0);
    }
    case FLD_FLOAT: {
      float v = text.empty() ? 0 : static_cast<float>(std::stod(text));
      return Fadd32(f, fid, reinterpret_cast<char *>(&v), 0);
    }
    case FLD_DOUBLE: {
      double v = text.empty() ? 0 : std::stod(text);
      return Fadd32(f, fid, reinterpret_cast<char *>(&v), 0);
    }
    default:
      Ferror32 = FTYPERR;
      return -1;
  }
}

// Convert one element into a field occurrence, growing the buffer on
// FNOSPACE; nested elements map to FLD_FML32 sub-buffers.
bool g_unknown_name = false;

int add_element(FBFR32 *&f, const element &child) {
  auto fid = Fldid32(const_cast<char *>(child.name.c_str()));
  if (fid == BADFLDID) {
    g_unknown_name = true;  // reported once the conversion completes
    return 0;  // unknown names are skipped, conversion continues
  }
  if (child.children.empty() && Fldtype32(fid) != FLD_FML32) {
    while (add_typed(f, fid, child.text) == -1) {
      if (Ferror32 != FNOSPACE) {
        return -1;
      }
      grow(f);
    }
    return 0;
  }
  while (true) {
    auto sub = Falloc32(16, 1024);
    for (auto &nested : child.children) {
      if (add_element(sub, nested) == -1) {
        Ffree32(sub);
        return -1;
      }
    }
    if (Fadd32(f, fid, reinterpret_cast<char *>(sub), 0) != -1) {
      Ffree32(sub);
      return 0;
    }
    int err = Ferror32;
    Ffree32(sub);
    if (err != FNOSPACE) {
      return -1;
    }
    grow(f);
  }
}

void convert(const element &root, FBFR32 *f) {
  for (auto &child : root.children) {
    if (add_element(f, child) == -1) {
      return;
    }
  }
}

std::string escape(const std::string &s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

void emit_value(std::string &out, FLDID32 fid, FLDOCC32 oc, FBFR32 *f) {
  int type = Fldtype32(fid);
  FLDLEN32 len = 0;
  char *val = CFfind32(f, fid, oc, &len, type);
  if (val == nullptr) {
    return;
  }
  char num[64];
  switch (type) {
    case FLD_STRING:
      out += escape(val);
      break;
    case FLD_CARRAY:
      out += escape(std::string(val, len));
      break;
    case FLD_CHAR:
      out += std::string(1, *val);
      break;
    case FLD_SHORT:
      snprintf(num, sizeof(num), "%hd", *reinterpret_cast<short *>(val));
      out += num;
      break;
    case FLD_LONG:
      snprintf(num, sizeof(num), "%ld", *reinterpret_cast<long *>(val));
      out += num;
      break;
    case FLD_FLOAT:
      snprintf(num, sizeof(num), "%g", *reinterpret_cast<float *>(val));
      out += num;
      break;
    case FLD_DOUBLE:
      snprintf(num, sizeof(num), "%g", *reinterpret_cast<double *>(val));
      out += num;
      break;
    default:
      break;
  }
}

void emit_field(std::string &out, FLDID32 fid, FLDOCC32 oc, FBFR32 *f) {
  const char *fname = Fname32(fid);
  if (fname == nullptr) {
    return;
  }
  std::string namebuf = fname;
  out += "<";
  out += namebuf;
  if (Fldtype32(fid) == FLD_FML32) {
    out += " Type=\"FML32\">";
    FLDLEN32 len = 0;
    auto sub = reinterpret_cast<FBFR32 *>(Ffind32(f, fid, oc, &len));
    if (sub != nullptr) {
      FLDID32 cfid = FIRSTFLDID;
      FLDOCC32 coc = 0;
      while (Fnext32(sub, &cfid, &coc, nullptr, nullptr) > 0) {
        emit_field(out, cfid, coc, sub);
      }
    }
  } else {
    out += ">";
    emit_value(out, fid, oc, f);
  }
  out += "</";
  out += namebuf;
  out += ">";
}

}  // namespace

int xml_to_fml32(char *xmlbuf, FBFR32 *fmlbuf, FBFR32 **out) {
  try {
    xmlparser p(xmlbuf != nullptr ? xmlbuf : "");
    auto root = p.parse_document();
    Finit32(fmlbuf, Fsizeof32(fmlbuf));
    g_unknown_name = false;
    convert(root, fmlbuf);
    if (g_unknown_name) {
      // the platform leaves FBADNAME sticky for skipped element names
      Ferror32 = FBADNAME;
    }
    if (out != nullptr) {
      *out = fmlbuf;
    }
    return 0;
  } catch (const std::exception &e) {
    (void)e;
    Ferror32 = FSYNTAX;
    return -1;
  }
}

int fml32_to_xml(FBFR32 *fmlbuf, char *xmlbuf, long maxlen) {
  std::string out =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>";
  out += "<FML32 Type=\"FML32\">";
  FLDID32 fid = FIRSTFLDID;
  FLDOCC32 oc = 0;
  while (Fnext32(fmlbuf, &fid, &oc, nullptr, nullptr) > 0) {
    emit_field(out, fid, oc, fmlbuf);
  }
  out += "</FML32>";
  if (maxlen < 0 || static_cast<long>(out.size() + 1) > maxlen) {
    Ferror32 = FNOSPACE;
    return -1;
  }
  memcpy(xmlbuf, out.c_str(), out.size() + 1);
  return 0;
}

}  // namespace fux::xml

int tpxmltofml32(char *xmlbuf, char *fmlbuf, FBFR32 **fml32buf,
                 char **extrerr, long flags) {
  (void)flags;
  if (extrerr != nullptr) {
    *extrerr = nullptr;
  }
  auto *target =
      fml32buf != nullptr && *fml32buf != nullptr
          ? *fml32buf
          : reinterpret_cast<FBFR32 *>(fmlbuf);
  if (target == nullptr) {
    target = Falloc32(16, 1024);
    if (fml32buf != nullptr) {
      *fml32buf = target;
    }
  }
  int rc = fux::xml::xml_to_fml32(xmlbuf, target, fml32buf);
  if (rc == -1 && extrerr != nullptr) {
    const char *msg = "Error occured during parsing XML.";
    *extrerr = static_cast<char *>(tpalloc(const_cast<char *>("STRING"),
                                           nullptr,
                                           static_cast<long>(strlen(msg) + 1)));
    if (*extrerr != nullptr) {
      strcpy(*extrerr, msg);
    }
  }
  return rc;
}

int tpfml32toxml(FBFR32 *fml32buf, char *xmlbuf, char *xmlflds,
                 char **extrerr, long flags) {
  (void)xmlflds;
  (void)flags;
  if (extrerr != nullptr) {
    *extrerr = nullptr;
  }
  if (fml32buf == nullptr || xmlbuf == nullptr) {
    return -1;
  }
  // the XML buffer capacity bounds the output, like the platform
  return fux::xml::fml32_to_xml(fml32buf, xmlbuf,
                                fux::mem::allocsize(xmlbuf));
}

