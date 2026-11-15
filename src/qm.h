#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <atmidefs.h>
#include <xa.h>

#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" struct xa_switch_t tuxq_switch;

namespace fux::qm {

namespace fs = std::filesystem;

// Housekeeping entries inside queue folders are dot-prefixed. User-supplied
// queue space and queue names must not start with '.' (see validate_*()).
constexpr const char *META_FILE = ".meta";
constexpr const char *LOCK_FILE = ".lock";
constexpr const char *TMP_PREFIX = ".tmp-";

struct qm_error : std::runtime_error {
  long diagnostic;
  qm_error(long diag, const std::string &msg)
      : std::runtime_error(msg), diagnostic(diag) {}
};

void validate_qspace_name(const std::string &name);
void validate_queue_name(const std::string &name);

fs::path qmconfig();
fs::path qspace_dir(const std::string &qspace);
fs::path queue_dir(const std::string &qspace, const std::string &qname);

using kv = std::map<std::string, std::string>;
kv read_meta(const fs::path &file);
void write_meta(const fs::path &file, const kv &meta);

void create_qspace(const std::string &qspace, const kv &meta);
void remove_qspace(const std::string &qspace);
void create_queue(const std::string &qspace, const std::string &qname,
                  const kv &meta);
void remove_queue(const std::string &qspace, const std::string &qname);
void rename_queue(const std::string &qspace, const std::string &from,
                  const std::string &to);

struct msg {
  std::string id;  // file name: prio.seq
  std::string corrid;
  std::string replyq;
  std::string failureq;
  long priority = 50;
  long enqtime = 0;  // epoch seconds
  long deqtime = 0;  // not available before, 0 = immediately
  long exptime = 0;  // expire at, 0 = never
  std::string payload;
};

msg read_msg(const fs::path &file, bool with_payload);
void write_msg(const fs::path &file, const msg &m);
void fsync_dir(const fs::path &dir);

// Auto-creates queue space/queue directories with default .meta when missing.
std::string enqueue(const std::string &qspace, const std::string &qname,
                    const TPQCTL *ctl, const std::string &payload);

struct dequeue_result {
  bool found = false;
  msg m;
};

// wait_ms > 0 polls until a message becomes available or the timeout.
dequeue_result dequeue(const std::string &qspace, const std::string &qname,
                       const TPQCTL *ctl, bool peek, long wait_ms);

std::vector<msg> list_msgs(const std::string &qspace,
                           const std::string &qname, bool with_payload);
std::vector<std::string> list_queues(const std::string &qspace);
std::vector<std::string> list_qspaces();

long purge_expired(const std::string &qspace, const std::string &qname);

}  // namespace fux::qm
