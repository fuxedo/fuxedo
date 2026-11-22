#include <userlog.h>
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>
#include <thread>

#include <atmi.h>

#include "misc.h"
#include "qm.h"

namespace fux::qm {

namespace {

std::string hex_encode(const char *data, size_t len) {
  static const char digits[] = "0123456789abcdef";
  std::string out(len * 2, '0');
  for (size_t i = 0; i < len; i++) {
    out[i * 2] = digits[(data[i] >> 4) & 0xf];
    out[i * 2 + 1] = digits[data[i] & 0xf];
  }
  return out;
}

std::string hex_decode(const std::string &hex) {
  auto val = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  std::string out(hex.size() / 2, '\0');
  for (size_t i = 0; i < out.size(); i++) {
    out[i] = (val(hex[i * 2]) << 4) | val(hex[i * 2 + 1]);
  }
  return out;
}

void validate_name(const std::string &name, size_t maxlen,
                   const std::string &what) {
  if (name.empty() || name.size() > maxlen) {
    throw qm_error(QMEINVAL, what + " name must be 1-" +
                                 std::to_string(maxlen) + " characters");
  }
  if (name[0] == '.') {
    throw qm_error(QMEINVAL, what + " name must not start with '.'");
  }
  for (char c : name) {
    if (c == '/' || c == '\\' || static_cast<unsigned char>(c) < 0x21 ||
        c == 0x7f) {
      throw qm_error(QMEINVAL, what + " name contains invalid character");
    }
  }
}

class flock_guard {
 public:
  explicit flock_guard(const fs::path &lockfile, bool exclusive) {
    fd_ = open(lockfile.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd_ == -1) {
      throw std::system_error(errno, std::system_category(), "open lock");
    }
    if (flock(fd_, exclusive ? LOCK_EX : LOCK_SH) == -1) {
      close(fd_);
      throw std::system_error(errno, std::system_category(), "flock");
    }
  }
  ~flock_guard() {
    flock(fd_, LOCK_UN);
    close(fd_);
  }

 private:
  int fd_;
};

void write_all(int fd, const char *data, size_t len) {
  size_t off = 0;
  while (off < len) {
    ssize_t n = write(fd, data + off, len - off);
    if (n == -1) {
      throw std::system_error(errno, std::system_category(), "write");
    }
    off += n;
  }
}

constexpr const char *MSG_MAGIC = "FUXQ1";

std::string serialize(const msg &m) {
  std::string out;
  out += MSG_MAGIC;
  out += "\ncorrid:" + hex_encode(m.corrid.data(), m.corrid.size());
  out += "\nreplyq:" + m.replyq;
  out += "\nfailq:" + m.failureq;
  out += "\npriority:" + std::to_string(m.priority);
  out += "\nenq:" + std::to_string(m.enqtime);
  out += "\ndeq:" + std::to_string(m.deqtime);
  out += "\nexp:" + std::to_string(m.exptime);
  out += "\n\n";
  return out;
}

msg parse(const std::string &file, const std::string &content,
          bool with_payload) {
  msg m;
  m.id = file;
  if (content.compare(0, strlen(MSG_MAGIC), MSG_MAGIC) != 0) {
    return m;
  }
  size_t pos = strlen(MSG_MAGIC) + 1;
  while (pos < content.size()) {
    size_t eol = content.find('\n', pos);
    if (eol == std::string::npos) break;
    std::string line = content.substr(pos, eol - pos);
    pos = eol + 1;
    if (line.empty()) {
      if (with_payload) {
        m.payload = content.substr(pos);
      }
      break;
    }
    auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    auto key = line.substr(0, colon);
    auto value = line.substr(colon + 1);
    if (key == "corrid") {
      m.corrid = hex_decode(value);
    } else if (key == "replyq") {
      m.replyq = value;
    } else if (key == "failq") {
      m.failureq = value;
    } else if (key == "priority") {
      m.priority = std::stol(value);
    } else if (key == "enq") {
      m.enqtime = std::stol(value);
    } else if (key == "deq") {
      m.deqtime = std::stol(value);
    } else if (key == "exp") {
      m.exptime = std::stol(value);
    }
  }
  return m;
}

bool is_msg_file(const std::string &name) {
  return !name.empty() && name[0] != '.';
}

long now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

kv default_qspace_meta(const std::string &qspace) {
  return {{"name", qspace}, {"ipckey", "0"}, {"pages", "0"}, {"errorq", ""}};
}

kv default_queue_meta() {
  return {{"order", "priority"}, {"retries", "0"}, {"retrydelay", "0"},
          {"hi", "100m"},       {"lo", "0m"},      {"cmd", ""}};
}

void ensure_queue(const std::string &qspace, const std::string &qname) {
  auto qdir = queue_dir(qspace, qname);
  std::error_code ec;
  fs::create_directories(qdir, ec);
  if (ec) {
    throw std::system_error(ec, "create queue " + qdir.string());
  }
  auto meta = qdir / META_FILE;
  if (!fs::exists(meta)) {
    write_meta(meta, default_queue_meta());
    fsync_dir(qdir);
  }
}

std::vector<std::string> scan_msg_ids(const fs::path &qdir) {
  std::vector<std::string> ids;
  for (const auto &e : fs::directory_iterator(qdir)) {
    if (e.is_regular_file() && is_msg_file(e.path().filename().string())) {
      ids.push_back(e.path().filename().string());
    }
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

long next_seq(const std::vector<std::string> &ids) {
  long seq = 0;
  for (const auto &id : ids) {
    auto dot = id.find('.');
    if (dot != std::string::npos) {
      seq = std::max(seq, std::stol(id.substr(dot + 1)));
    }
  }
  return seq + 1;
}

void move_to_failureq(const std::string &qspace, const fs::path &qdir,
                      const msg &m) {
  if (m.failureq.empty()) {
    fs::remove(qdir / m.id);
    return;
  }
  ensure_queue(qspace, m.failureq);
  auto to = queue_dir(qspace, m.failureq);
  fs::rename(qdir / m.id, to / m.id);
  fsync_dir(to);
}

// Returns false if the message is not visible now (wrong id/corrid, not
// yet available); messages past expiry are moved to the failure queue and
// skipped.
bool visible(const std::string &qspace, const fs::path &qdir,
             const std::string &id, const TPQCTL *ctl, msg *out) {
  auto m = read_msg(qdir / id, true);
  if (ctl != nullptr) {
    if ((ctl->flags & TPQGETBYMSGID) != 0 &&
        id != std::string(ctl->msgid, strnlen(ctl->msgid, TMMSGIDLEN))) {
      return false;
    }
    if ((ctl->flags & TPQGETBYCORRID) != 0) {
      std::string want(ctl->corrid, strnlen(ctl->corrid, TMCORRIDLEN));
      if (m.corrid != want) {
        return false;
      }
    }
  }
  long t = now();
  if (m.deqtime > t) {
    return false;
  }
  // an expired message moves to its failure queue - unless it already
  // sits there (the move keeps the original expiry, which must not
  // retrigger on the failure queue itself)
  if (m.exptime != 0 && m.exptime <= t &&
      qdir.filename() != m.failureq) {
    move_to_failureq(qspace, qdir, m);
    fsync_dir(qdir);
    return false;
  }
  *out = std::move(m);
  return true;
}

}  // namespace

void fsync_dir(const fs::path &dir) {
  int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd == -1) {
    throw std::system_error(errno, std::system_category(), "open dir");
  }
  if (fsync(fd) == -1) {
    close(fd);
    throw std::system_error(errno, std::system_category(), "fsync dir");
  }
  close(fd);
}

msg read_msg(const fs::path &file, bool with_payload) {
  struct stat st;
  if (stat(file.c_str(), &st) == -1) {
    throw std::system_error(errno, std::system_category(),
                            "stat " + file.string());
  }
  std::string content(st.st_size, '\0');
  int fd = open(file.c_str(), O_RDONLY);
  if (fd == -1) {
    throw std::system_error(errno, std::system_category(),
                            "open " + file.string());
  }
  size_t off = 0;
  while (off < content.size()) {
    ssize_t n = read(fd, &content[off], content.size() - off);
    if (n <= 0) break;
    off += n;
  }
  close(fd);
  return parse(file.filename().string(), content, with_payload);
}

void write_msg(const fs::path &file, const msg &m) {
  std::string content = serialize(m);
  content += m.payload;
  int fd = open(file.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
  if (fd == -1) {
    throw std::system_error(errno, std::system_category(), "open msg");
  }
  write_all(fd, content.data(), content.size());
  fsync(fd);
  close(fd);
}

void validate_qspace_name(const std::string &name) {
  validate_name(name, TMQSNAMELEN, "queue space");
}

void validate_queue_name(const std::string &name) {
  validate_name(name, TMQNAMELEN, "queue");
}

fs::path qmconfig() {
  auto value = std::getenv("QMCONFIG");
  if (value == nullptr || *value == '\0') {
    throw qm_error(QMEINVAL, "QMCONFIG environment variable not set");
  }
  return fs::path(value);
}

fs::path qspace_dir(const std::string &qspace) {
  validate_qspace_name(qspace);
  return qmconfig() / qspace;
}

fs::path queue_dir(const std::string &qspace, const std::string &qname) {
  validate_queue_name(qname);
  return qspace_dir(qspace) / qname;
}

kv read_meta(const fs::path &file) {
  kv meta;
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    auto pos = line.find('=');
    if (pos != std::string::npos) {
      meta[line.substr(0, pos)] = line.substr(pos + 1);
    }
  }
  return meta;
}

void write_meta(const fs::path &file, const kv &meta) {
  std::string tmp = file.string() + TMP_PREFIX;
  int fd = open(tmp.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
  if (fd == -1) {
    throw std::system_error(errno, std::system_category(), "open meta");
  }
  for (const auto &[k, v] : meta) {
    write_all(fd, k.c_str(), k.size());
    write_all(fd, "=", 1);
    write_all(fd, v.c_str(), v.size());
    write_all(fd, "\n", 1);
  }
  fsync(fd);
  close(fd);
  fs::rename(tmp, file);
  fsync_dir(file.parent_path());
}

void create_qspace(const std::string &qspace, const kv &meta) {
  auto dir = qspace_dir(qspace);
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    throw std::system_error(ec, "create queue space " + dir.string());
  }
  write_meta(dir / META_FILE, meta);
}

void remove_qspace(const std::string &qspace) {
  auto dir = qspace_dir(qspace);
  if (!fs::exists(dir)) {
    throw qm_error(QMEBADQUEUE, "queue space " + qspace + " does not exist");
  }
  fs::remove_all(dir);
  fsync_dir(qmconfig());
}

void create_queue(const std::string &qspace, const std::string &qname,
                  const kv &meta) {
  auto qsdir = qspace_dir(qspace);
  if (!fs::exists(qsdir / META_FILE)) {
    create_qspace(qspace, default_qspace_meta(qspace));
  }
  auto qdir = qsdir / qname;
  std::error_code ec;
  fs::create_directories(qdir, ec);
  if (ec) {
    throw std::system_error(ec, "create queue " + qdir.string());
  }
  write_meta(qdir / META_FILE, meta);
}

void remove_queue(const std::string &qspace, const std::string &qname) {
  auto qdir = queue_dir(qspace, qname);
  if (!fs::exists(qdir)) {
    throw qm_error(QMEBADQUEUE, "queue " + qname + " does not exist");
  }
  fs::remove_all(qdir);
  fsync_dir(qspace_dir(qspace));
}

void rename_queue(const std::string &qspace, const std::string &from,
                  const std::string &to) {
  auto fromdir = queue_dir(qspace, from);
  if (!fs::exists(fromdir)) {
    throw qm_error(QMEBADQUEUE, "queue " + from + " does not exist");
  }
  fs::rename(fromdir, queue_dir(qspace, to));
  fsync_dir(qspace_dir(qspace));
}

std::string enqueue(const std::string &qspace, const std::string &qname,
                    const TPQCTL *ctl, const std::string &payload) {
  auto qdir = queue_dir(qspace, qname);
  ensure_queue(qspace, qname);

  flock_guard lock(qdir / LOCK_FILE, true);

  msg m;
  m.enqtime = now();
  long priority = 50;
  if (ctl != nullptr) {
    if ((ctl->flags & TPQPRIORITY) != 0) {
      priority = ctl->priority;
      if (priority < 1 || priority > 100) {
        throw qm_error(QMEINVAL, "priority must be in range 1-100");
      }
    }
    if ((ctl->flags & TPQTOP) != 0) {
      priority = 1;
    }
    if ((ctl->flags & TPQCORRID) != 0) {
      m.corrid.assign(ctl->corrid, strnlen(ctl->corrid, TMCORRIDLEN));
    }
    if ((ctl->flags & TPQREPLYQ) != 0) {
      m.replyq = ctl->replyqueue;
    }
    if ((ctl->flags & TPQFAILUREQ) != 0) {
      m.failureq = ctl->failurequeue;
    }
    if ((ctl->flags & TPQTIME_ABS) != 0) {
      m.deqtime = ctl->deq_time;
    } else if ((ctl->flags & TPQTIME_REL) != 0) {
      m.deqtime = now() + ctl->deq_time;
    }
    if ((ctl->flags & TPQEXPTIME_ABS) != 0) {
      m.exptime = ctl->exp_time;
    } else if ((ctl->flags & TPQEXPTIME_REL) != 0) {
      m.exptime = now() + ctl->exp_time;
    }
  }
  m.priority = priority;

  auto ids = scan_msg_ids(qdir);
  long seq = next_seq(ids);
  char idbuf[64];
  snprintf(idbuf, sizeof(idbuf), "%03ld.%016ld", priority, seq);
  m.id = idbuf;

  std::string content = serialize(m);
  content += payload;

  auto tmp = qdir / (TMP_PREFIX + m.id);
  int fd = open(tmp.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
  if (fd == -1) {
    throw std::system_error(errno, std::system_category(), "open msg");
  }
  write_all(fd, content.data(), content.size());
  fsync(fd);
  close(fd);
  fs::rename(tmp, qdir / m.id);
  fsync_dir(qdir);
  return m.id;
}


fs::path tx_dir(const std::string &qspace, const std::string &gtrid) {
  return qspace_dir(qspace) / ".tx" / gtrid;
}

std::string stage_enqueue(const std::string &qspace, const std::string &qname,
                          const TPQCTL *ctl, const std::string &payload,
                          const std::string &gtrid) {
  ensure_queue(qspace, qname);
  auto dir = tx_dir(qspace, gtrid);
  fs::create_directories(dir);

  msg m;
  m.enqtime = now();
  long priority = 50;
  if (ctl != nullptr) {
    if ((ctl->flags & TPQPRIORITY) != 0) {
      priority = ctl->priority;
    }
    if ((ctl->flags & TPQTOP) != 0) {
      priority = 1;
    }
    if ((ctl->flags & TPQCORRID) != 0) {
      m.corrid.assign(ctl->corrid, strnlen(ctl->corrid, TMCORRIDLEN));
    }
    if ((ctl->flags & TPQREPLYQ) != 0) {
      m.replyq = ctl->replyqueue;
    }
    if ((ctl->flags & TPQFAILUREQ) != 0) {
      m.failureq = ctl->failurequeue;
    }
    if ((ctl->flags & TPQTIME_ABS) != 0) {
      m.deqtime = ctl->deq_time;
    } else if ((ctl->flags & TPQTIME_REL) != 0) {
      m.deqtime = now() + ctl->deq_time;
    }
    if ((ctl->flags & TPQEXPTIME_ABS) != 0) {
      m.exptime = ctl->exp_time;
    } else if ((ctl->flags & TPQEXPTIME_REL) != 0) {
      m.exptime = now() + ctl->exp_time;
    }
  }
  m.priority = priority;

  auto ids = scan_msg_ids(queue_dir(qspace, qname));
  char idbuf[64];
  snprintf(idbuf, sizeof(idbuf), "%03ld.%016ld", priority, next_seq(ids));
  m.id = idbuf;

  m.payload = payload;
  auto target = dir / ("enq_" + qname + "_" + m.id);
  write_msg(target, m);
  fsync_dir(dir);
  return m.id;
}

void stage_dequeue(const std::string &qspace, const std::string &qname,
                   const std::string &msgid, const std::string &gtrid) {
  auto dir = tx_dir(qspace, gtrid);
  fs::create_directories(dir);
  auto from = queue_dir(qspace, qname) / msgid;
  auto to = dir / ("deq_" + qname + "_" + msgid);
  fs::rename(from, to);
  fsync_dir(dir);
  fsync_dir(queue_dir(qspace, qname));
}

dequeue_result dequeue(const std::string &qspace, const std::string &qname,
                       const TPQCTL *ctl, bool peek, long wait_ms) {
  auto qdir = queue_dir(qspace, qname);
  if (!fs::exists(qdir)) {
    throw qm_error(QMEBADQUEUE, "queue " + qname + " does not exist");
  }

  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(wait_ms);
  for (;;) {
    {
      flock_guard lock(qdir / LOCK_FILE, true);
      auto ids = scan_msg_ids(qdir);
      for (const auto &id : ids) {
        msg m;
        if (!visible(qspace, qdir, id, ctl, &m)) {
          continue;
        }
        if (!peek) {
          fs::remove(qdir / id);
          fsync_dir(qdir);
        }
        dequeue_result result;
        result.found = true;
        result.m = std::move(m);
        return result;
      }
    }
    if (wait_ms <= 0 || std::chrono::steady_clock::now() >= deadline) {
      return {};
    }
    // one-second quantum, like the platform's queue servers
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

std::vector<msg> list_msgs(const std::string &qspace, const std::string &qname,
                           bool with_payload) {
  auto qdir = queue_dir(qspace, qname);
  if (!fs::exists(qdir)) {
    throw qm_error(QMEBADQUEUE, "queue " + qname + " does not exist");
  }
  flock_guard lock(qdir / LOCK_FILE, false);
  std::vector<msg> out;
  for (const auto &id : scan_msg_ids(qdir)) {
    try {
      out.push_back(read_msg(qdir / id, with_payload));
    } catch (qm_error &) {
      // expired while listing
    }
  }
  return out;
}

std::vector<std::string> list_queues(const std::string &qspace) {
  auto qsdir = qspace_dir(qspace);
  if (!fs::exists(qsdir)) {
    throw qm_error(QMEBADQUEUE, "queue space " + qspace + " does not exist");
  }
  std::vector<std::string> out;
  for (const auto &e : fs::directory_iterator(qsdir)) {
    if (e.is_directory() && e.path().filename().string()[0] != '.') {
      out.push_back(e.path().filename().string());
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<std::string> list_qspaces() {
  auto root = qmconfig();
  std::vector<std::string> out;
  for (const auto &e : fs::directory_iterator(root)) {
    if (e.is_directory() && e.path().filename().string()[0] != '.') {
      out.push_back(e.path().filename().string());
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

long purge_expired(const std::string &qspace, const std::string &qname) {
  auto qdir = queue_dir(qspace, qname);
  if (!fs::exists(qdir)) {
    return 0;
  }
  flock_guard lock(qdir / LOCK_FILE, true);
  long purged = 0;
  long t = now();
  for (const auto &id : scan_msg_ids(qdir)) {
    auto m = read_msg(qdir / id, false);
    if (m.exptime != 0 && m.exptime <= t) {
      move_to_failureq(qspace, qdir, m);
      purged++;
    }
  }
  if (purged > 0) {
    fsync_dir(qdir);
  }
  return purged;
}

}  // namespace fux::qm
