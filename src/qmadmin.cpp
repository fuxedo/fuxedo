// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <atmi.h>

#include "misc.h"
#include "qm.h"

namespace {

struct device {
  std::string name;
  long offset;
  long size;
};
std::vector<device> devices;

std::string current_qspace;
std::string current_queue;  // set by qset
bool echo_mode = false;
bool verbose_mode = false;

std::string trim(const std::string &s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::vector<std::string> tokenize(const std::string &line) {
  std::vector<std::string> out;
  std::string cur;
  char quote = 0;
  for (char c : line) {
    if (quote != 0) {
      if (c == quote) {
        quote = 0;
      } else {
        cur += c;
      }
    } else if (c == '\'' || c == '"') {
      quote = c;
    } else if (::isspace(static_cast<unsigned char>(c))) {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

std::string ask(const std::string &prompt, const std::string &def = "") {
  std::cout << prompt << std::flush;
  std::string line;
  if (!std::getline(std::cin, line)) {
    throw std::runtime_error("end of input");
  }
  line = trim(line);
  return line.empty() ? def : line;
}

bool confirm(const std::string &what) {
  std::string answer = ask(what + " y/n/q: ");
  return answer == "y" || answer == "Y";
}

void require_qspace() {
  if (current_qspace.empty()) {
    std::cout << "QMENOTOPEN: a queue space must be opened" << std::endl;
    throw std::runtime_error("qspace");
  }
}

long to_long(const std::string &s, long def = 0) {
  try {
    return std::stol(s);
  } catch (...) {
    return def;
  }
}

std::string hexify(const std::string &data) {
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (unsigned char c : data) {
    out += digits[c >> 4];
    out += digits[c & 0xf];
  }
  return out;
}

std::string fmt_time(long t) {
  if (t == 0) return "-";
  char buf[32];
  struct tm tmv;
  time_t tt = t;
  localtime_r(&tt, &tmv);
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
  return buf;
}

// Oracle selection time format: YY[MM[DD[HH[MM[SS]]]]]
long parse_qlong(const std::string &s) {
  return to_long(s);
}

void cmd_help(const std::vector<std::string> &) {
  std::cout
      << "General: echo help verbose quit\n"
      << "Devices (emulated): crdl dsdl lidl livtoc llt ls\n"
      << "Queue spaces: qspacecreate qspacechange qspacedelete qopen qclose\n"
      << "Queues: qcreate qchange qdelete qinfo qlist\n"
      << "Messages: qscan qprint qdeletemsg qchangeprio qchangequeue\n"
      << "          qchangetime qchangeexptime\n"
      << "Transactions (emulated): qcommittrans qaborttrans qprinttrans\n"
      << "Selection: qset qscan" << std::endl;
}

void cmd_echo(const std::vector<std::string> &args) {
  if (args.empty()) {
    std::cout << "Echo mode is " << (echo_mode ? "on" : "off") << std::endl;
    return;
  }
  echo_mode = args[0] == "on" || args[0] == "y";
}

void cmd_verbose(const std::vector<std::string> &) {
  verbose_mode = !verbose_mode;
  std::cout << "Verbose mode is " << (verbose_mode ? "on" : "off")
            << std::endl;
}

void cmd_crdl(const std::vector<std::string> &args) {
  if (args.size() < 3) {
    std::cout << "Usage: crdl device offset size" << std::endl;
    return;
  }
  devices.push_back({args[0], to_long(args[1]), to_long(args[2])});
  std::cout << "Created device " << args[0] << ", offset " << args[1]
            << ", size " << args[2] << std::endl;
}

void cmd_dsdl(const std::vector<std::string> &args) {
  if (args.empty()) {
    std::cout << "Usage: dsdl device_index" << std::endl;
    return;
  }
  size_t idx = to_long(args[0]);
  if (idx >= devices.size()) {
    std::cout << "DSDL failed: invalid device index" << std::endl;
    return;
  }
  devices.erase(devices.begin() + idx);
  std::cout << "Destroyed device list entry " << args[0] << std::endl;
}

void cmd_lidl(const std::vector<std::string> &) {
  if (devices.empty()) {
    std::cout << "Device list is empty" << std::endl;
    return;
  }
  for (size_t i = 0; i < devices.size(); i++) {
    const auto &d = devices[i];
    std::cout << "device " << i << ":\n"
              << "        name: " << d.name << "\n"
              << "        start: " << d.offset << "\n"
              << "        size: " << d.size << std::endl;
  }
}

void cmd_dummy_devices(const std::string &name,
                       const std::vector<std::string> &) {
  std::cout << name << ": not supported for filesystem-based queue spaces"
            << std::endl;
}

void cmd_qspacecreate(const std::vector<std::string> &args) {
  std::vector<std::string> positional;
  for (size_t i = 0; i < args.size(); i++) {
    if (args[i] == "-A" || args[i] == "-n" || args[i] == "-o") {
      i++;  // accepted, not persisted
    } else {
      positional.push_back(args[i]);
    }
  }
  auto next = [&](size_t n, const std::string &prompt,
                    const std::string &def = "") {
    return positional.size() > n ? positional[n] : ask(prompt, def);
  };

  std::string name = next(0, "Queue space name: ");
  std::string ipckey = next(1, "IPC Key for queue space: ");
  std::string pages = next(2, "Size of queue space in disk pages: ");
  std::string queues = next(3, "Number of queues in queue space: ");
  std::string trans = next(4, "Number of concurrent transactions in queue space: ");
  std::string procs = next(5, "Number of concurrent processes in queue space: ");
  std::string messages = next(6, "Number of messages in queue space: ");
  std::string errorq = next(7, "Error queue name: ");
  std::string inityn = next(8, "Initialize extents (y/n) [n]: ", "n");
  std::string blocking = next(9, "Blocking factor [16]: ", "16");

  try {
    fux::qm::create_qspace(name, {{"name", name},
                                  {"ipckey", ipckey},
                                  {"pages", pages},
                                  {"queues", queues},
                                  {"trans", trans},
                                  {"procs", procs},
                                  {"messages", messages},
                                  {"errorq", errorq},
                                  {"blocking", blocking}});
    if (inityn == "y" || inityn == "Y") {
      std::cout << "...................." << std::endl;
    }
    std::cout << "Queue space " << name << " created" << std::endl;
  } catch (const std::exception &e) {
    std::cout << "QMEOS: " << e.what() << std::endl;
  }
}

void cmd_qspacechange(const std::vector<std::string> &args) {
  std::vector<std::string> positional;
  for (size_t i = 0; i < args.size(); i++) {
    if (args[i] == "-A" || args[i] == "-H" || args[i] == "-C" ||
        args[i] == "-O" || args[i] == "-Q" || args[i] == "-f" ||
        args[i] == "-n" || args[i] == "-o") {
      i++;
    } else {
      positional.push_back(args[i]);
    }
  }
  std::string name = positional.empty() ? current_qspace : positional[0];
  if (name.empty()) {
    std::cout << "Queue space name: " << std::flush;
    std::getline(std::cin, name);
    name = trim(name);
  }
  try {
    auto meta = fux::qm::read_meta(fux::qm::qspace_dir(name) /
                                   fux::qm::META_FILE);
    const char *keys[] = {"ipckey", "trans", "procs", "messages",
                          "errorq", "blocking"};
    for (size_t i = 0; i < 6 && i + 1 < positional.size(); i++) {
      meta[keys[i]] = positional[i + 1];
    }
    fux::qm::write_meta(fux::qm::qspace_dir(name) / fux::qm::META_FILE, meta);
    std::cout << "Queue space " << name << " changed" << std::endl;
  } catch (const std::exception &e) {
    std::cout << "QMEBADQUEUE: " << e.what() << std::endl;
  }
}

void cmd_qspacedelete(const std::vector<std::string> &args) {
  bool yes = false;
  std::string name;
  for (const auto &a : args) {
    if (a == "-y") {
      yes = true;
    } else {
      name = a;
    }
  }
  if (name.empty()) {
    name = ask("Queue space name: ");
  }
  try {
    if (!yes && !confirm("Really delete queue space " + name + "?")) {
      return;
    }
    fux::qm::remove_qspace(name);
    if (current_qspace == name) {
      current_qspace.clear();
      current_queue.clear();
    }
    std::cout << "Queue space " << name << " deleted" << std::endl;
  } catch (const std::exception &e) {
    std::cout << "QMEBADQUEUE: " << e.what() << std::endl;
  }
}

void cmd_qopen(const std::vector<std::string> &args) {
  std::string name = args.empty() ? ask("Queue space name: ") : args[0];
  try {
    if (!fux::qm::fs::exists(fux::qm::qspace_dir(name) / fux::qm::META_FILE)) {
      std::cout << "QMEBADQUEUE: queue space " << name << " does not exist"
                << std::endl;
      return;
    }
    current_qspace = name;
    current_queue.clear();
  } catch (const std::exception &e) {
    std::cout << "QMEINVAL: " << e.what() << std::endl;
  }
}

void cmd_qclose(const std::vector<std::string> &) {
  current_qspace.clear();
  current_queue.clear();
}

fux::qm::kv queue_defaults() {
  return {{"order", "priority"}, {"retries", "0"}, {"retrydelay", "0"},
          {"hi", "100%"},       {"lo", "0m"},     {"cmd", ""}};
}

void queue_prompted(const std::vector<std::string> &args, bool change) {
  auto next = [&](size_t n, const std::string &prompt,
                    const std::string &def = "") {
    return args.size() > n ? args[n] : ask(prompt, def);
  };
  size_t n = change ? 1 : 0;  // qchange takes queue_name as args[0]
  std::string name = change ? (args.empty() ? ask("Queue name: ") : args[0])
                            : next(n++, "Queue name: ");
  fux::qm::kv meta = change ? fux::qm::read_meta(fux::qm::queue_dir(
                                  current_qspace, name) / fux::qm::META_FILE)
                            : queue_defaults();
  meta["name"] = name;
  meta["order"] = next(n++, "Queue order (priority/time/fifo/lifo): ",
                       meta["order"]);
  meta["outoforder"] = next(
      n++, "Out-of-order enqueueing (top/msgid/none) [none]:", "none");
  meta["retries"] = next(n++, "Retries [0]: ", meta["retries"]);
  meta["retrydelay"] = next(n++, "Retry delay in seconds [0]: ",
                            meta["retrydelay"]);
  meta["hi"] = next(n++,
                    "High capacity warning limit [100%]: ",
                    meta["hi"]);
  meta["lo"] = next(n++,
                    "Low capacity warning limit [0m]: ",
                    meta["lo"]);
  meta["cmd"] = next(n++, "Capacity command []: ", meta["cmd"]);

  fux::qm::create_queue(current_qspace, name, meta);
  std::cout << "Queue " << name << (change ? " changed" : " created")
            << std::endl;
}

void cmd_qcreate(const std::vector<std::string> &args) {
  try {
    require_qspace();
    queue_prompted(args, false);
  } catch (const std::runtime_error &) {
  } catch (const std::exception &e) {
    std::cout << "QMEOS: " << e.what() << std::endl;
  }
}

void cmd_qchange(const std::vector<std::string> &args) {
  try {
    require_qspace();
    queue_prompted(args, true);
  } catch (const std::runtime_error &) {
  } catch (const std::exception &e) {
    std::cout << "QMEOS: " << e.what() << std::endl;
  }
}

void cmd_qdelete(const std::vector<std::string> &args) {
  try {
    require_qspace();
    bool yes = false;
    std::string name;
    for (const auto &a : args) {
      if (a == "-y") {
        yes = true;
      } else {
        name = a;
      }
    }
    if (name.empty()) {
      name = ask("Queue name: ");
    }
    if (!yes && !confirm("Really delete queue " + name + "?")) {
      return;
    }
    fux::qm::remove_queue(current_qspace, name);
    if (current_queue == name) {
      current_queue.clear();
    }
    std::cout << "Queue " << name << " deleted" << std::endl;
  } catch (const std::runtime_error &) {
  } catch (const std::exception &e) {
    std::cout << "QMEBADQUEUE: " << e.what() << std::endl;
  }
}

void cmd_qinfo(const std::vector<std::string> &args) {
  try {
    require_qspace();
    std::string name = args.empty() ? current_qspace : args[0];
    auto meta = fux::qm::read_meta(fux::qm::qspace_dir(name) / fux::qm::META_FILE);
    std::cout << "Queue space: " << name << std::endl;
    for (const auto &[k, v] : meta) {
      std::cout << "  " << k << ": " << v << std::endl;
    }
    for (const auto &q : fux::qm::list_queues(name)) {
      auto msgs = fux::qm::list_msgs(name, q, false);
      auto qmeta = fux::qm::read_meta(fux::qm::queue_dir(name, q) /
                                      fux::qm::META_FILE);
      std::cout << "Queue " << q << ": " << msgs.size() << " messages, order "
                << qmeta["order"] << std::endl;
    }
  } catch (const std::runtime_error &) {
  } catch (const std::exception &e) {
    std::cout << "QMEBADQUEUE: " << e.what() << std::endl;
  }
}

void cmd_qlist(const std::vector<std::string> &) {
  try {
    require_qspace();
    for (const auto &q : fux::qm::list_queues(current_qspace)) {
      auto msgs = fux::qm::list_msgs(current_qspace, q, false);
      std::cout << q << " (" << msgs.size() << " messages)" << std::endl;
    }
  } catch (const std::runtime_error &) {
  } catch (const std::exception &e) {
    std::cout << "QMEBADQUEUE: " << e.what() << std::endl;
  }
}

const fux::qm::msg *find_msg(const std::vector<fux::qm::msg> &msgs,
                             const std::string &id) {
  for (const auto &m : msgs) {
    if (m.id == id) return &m;
  }
  return nullptr;
}

void cmd_qset(const std::vector<std::string> &args) {
  try {
    require_qspace();
  } catch (const std::runtime_error &) {
    return;
  }
  if (args.empty()) {
    std::cout << "Current queue: " << (current_queue.empty() ? "(none)" : current_queue)
              << std::endl;
    return;
  }
  current_queue = args[0];
}

void cmd_qscan(const std::vector<std::string> &args) {
  try {
    require_qspace();
    std::string queue = current_queue;
    std::string msgid, corrid;
    for (size_t i = 0; i < args.size(); i++) {
      if (args[i] == "-q" && i + 1 < args.size()) {
        queue = args[++i];
      } else if (args[i] == "-m" && i + 1 < args.size()) {
        msgid = args[++i];
      } else if (args[i] == "-i" && i + 1 < args.size()) {
        corrid = args[++i];
      }
    }
    if (queue.empty()) {
      std::cout << "Queue name must be set (qset or qscan -q)" << std::endl;
      return;
    }
    auto msgs = fux::qm::list_msgs(current_qspace, queue, false);
    for (const auto &m : msgs) {
      if (!msgid.empty() && m.id != msgid) continue;
      if (!corrid.empty() && m.corrid != corrid) continue;
      std::cout << m.id << " prio=" << m.priority
                << " corrid=" << (m.corrid.empty() ? "-" : hexify(m.corrid))
                << " enq=" << fmt_time(m.enqtime);
      if (m.deqtime != 0) std::cout << " deq=" << fmt_time(m.deqtime);
      if (m.exptime != 0) std::cout << " exp=" << fmt_time(m.exptime);
      std::cout << std::endl;
    }
  } catch (const std::runtime_error &) {
  } catch (const std::exception &e) {
    std::cout << "QMEBADQUEUE: " << e.what() << std::endl;
  }
}

void cmd_qprint(const std::vector<std::string> &args) {
  try {
    require_qspace();
    if (args.empty()) {
      std::cout << "Usage: qprint message_id" << std::endl;
      return;
    }
    std::string queue = current_queue;
    if (queue.empty()) {
      // search all queues for the message
      for (const auto &q : fux::qm::list_queues(current_qspace)) {
        auto msgs = fux::qm::list_msgs(current_qspace, q, true);
        if (find_msg(msgs, args[0]) != nullptr) {
          queue = q;
          break;
        }
      }
    }
    auto msgs = fux::qm::list_msgs(current_qspace, queue, true);
    auto m = find_msg(msgs, args[0]);
    if (m == nullptr) {
      std::cout << "QMEBADMSGID: message " << args[0] << " not found"
                << std::endl;
      return;
    }
    std::cout << "Message id: " << m->id << "\n"
              << "  priority: " << m->priority << "\n"
              << "  corrid: "
              << (m->corrid.empty() ? "-" : hexify(m->corrid)) << "\n"
              << "  replyq: " << (m->replyq.empty() ? "-" : m->replyq) << "\n"
              << "  failureq: " << (m->failureq.empty() ? "-" : m->failureq)
              << "\n"
              << "  enqueued: " << fmt_time(m->enqtime) << "\n"
              << "  payload: " << m->payload.size() << " bytes" << std::endl;
  } catch (const std::runtime_error &) {
  } catch (const std::exception &e) {
    std::cout << "QMEBADQUEUE: " << e.what() << std::endl;
  }
}

void cmd_qdeletemsg(const std::vector<std::string> &args) {
  try {
    require_qspace();
    bool yes = false;
    std::string msgid;
    for (const auto &a : args) {
      if (a == "-y") {
        yes = true;
      } else {
        msgid = a;
      }
    }
    std::string queue = current_queue;
    if (queue.empty()) {
      std::cout << "Queue name must be set (qset)" << std::endl;
      return;
    }
    if (!yes &&
        !confirm("Really delete messages from queue " + queue + "?")) {
      return;
    }
    if (!msgid.empty() && msgid != "all") {
      auto qdir = fux::qm::queue_dir(current_qspace, queue);
      fux::qm::fs::remove(qdir / msgid);
      fux::qm::fsync_dir(qdir);
      std::cout << "Message " << msgid << " deleted" << std::endl;
      return;
    }
    auto msgs = fux::qm::list_msgs(current_qspace, queue, false);
    auto qdir = fux::qm::queue_dir(current_qspace, queue);
    for (const auto &m : msgs) {
      fux::qm::fs::remove(qdir / m.id);
    }
    fux::qm::fsync_dir(qdir);
    std::cout << msgs.size() << " messages deleted" << std::endl;
  } catch (const std::runtime_error &) {
  } catch (const std::exception &e) {
    std::cout << "QMEBADQUEUE: " << e.what() << std::endl;
  }
}

void load_modify_store(const std::string &queue, const std::string &msgid,
                       const std::function<void(fux::qm::msg &)> &fn) {
  auto qdir = fux::qm::queue_dir(current_qspace, queue);
  auto file = qdir / msgid;
  auto m = fux::qm::read_msg(file, true);
  fn(m);
  fux::qm::write_msg(file, m);
}

void cmd_qchangeprio(const std::vector<std::string> &args) {
  try {
    require_qspace();
    if (current_queue.empty() || args.size() < 2) {
      std::cout << "Usage: qchangeprio new_priority message_id (queue set via qset)"
                << std::endl;
      return;
    }
    long prio = to_long(args[0]);
    auto qdir = fux::qm::queue_dir(current_qspace, current_queue);
    auto m = fux::qm::read_msg(qdir / args[1], true);
    char idbuf[64];
    snprintf(idbuf, sizeof(idbuf), "%03ld.%s", prio,
             args[1].substr(args[1].find('.') + 1).c_str());
    m.priority = prio;
    fux::qm::write_msg(qdir / idbuf, m);
    fux::qm::fs::remove(qdir / args[1]);
    fux::qm::fsync_dir(qdir);
    std::cout << "Message " << args[1] << " priority changed to " << prio
              << std::endl;
  } catch (const std::runtime_error &) {
  } catch (const std::exception &e) {
    std::cout << "QMEBADMSGID: " << e.what() << std::endl;
  }
}

void cmd_qchangetime(const std::vector<std::string> &args) {
  try {
    require_qspace();
    if (current_queue.empty() || args.size() < 2) {
      std::cout << "Usage: qchangetime new_time message_id" << std::endl;
      return;
    }
    long t = args[0] == "none" ? 0 : parse_qlong(args[0]);
    load_modify_store(current_queue, args[1],
                      [&](auto &m) { m.deqtime = t; });
    std::cout << "Message " << args[1] << " dequeue time changed" << std::endl;
  } catch (const std::runtime_error &) {
  } catch (const std::exception &e) {
    std::cout << "QMEBADMSGID: " << e.what() << std::endl;
  }
}

void cmd_qchangeexptime(const std::vector<std::string> &args) {
  try {
    require_qspace();
    if (current_queue.empty() || args.size() < 2) {
      std::cout << "Usage: qchangeexptime new_time message_id" << std::endl;
      return;
    }
    long t = args[0] == "none" ? 0 : parse_qlong(args[0]);
    load_modify_store(current_queue, args[1],
                      [&](auto &m) { m.exptime = t; });
    std::cout << "Message " << args[1] << " expiration time changed"
              << std::endl;
  } catch (const std::runtime_error &) {
  } catch (const std::exception &e) {
    std::cout << "QMEBADMSGID: " << e.what() << std::endl;
  }
}

void cmd_qchangequeue(const std::vector<std::string> &args) {
  try {
    require_qspace();
    if (current_queue.empty() || args.empty()) {
      std::cout << "Usage: qchangequeue new_queue_name" << std::endl;
      return;
    }
    auto from = fux::qm::queue_dir(current_qspace, current_queue);
    auto msgs = fux::qm::list_msgs(current_qspace, current_queue, false);
    if (!confirm("Really move " + std::to_string(msgs.size()) +
                 " messages to queue " + args[0] + "?")) {
      return;
    }
    fux::qm::create_queue(current_qspace, args[0], queue_defaults());
    auto to = fux::qm::queue_dir(current_qspace, args[0]);
    for (const auto &m : msgs) {
      fux::qm::fs::rename(from / m.id, to / m.id);
    }
    fux::qm::fsync_dir(from);
    fux::qm::fsync_dir(to);
    std::cout << msgs.size() << " messages moved to queue " << args[0]
              << std::endl;
  } catch (const std::runtime_error &) {
  } catch (const std::exception &e) {
    std::cout << "QMEBADQUEUE: " << e.what() << std::endl;
  }
}

void cmd_qtrans(const std::string &name, const std::vector<std::string> &) {
  if (name == "qprinttrans") {
    std::cout << "No transactions found" << std::endl;
  } else {
    std::cout << name
              << ": transactions are not supported by filesystem-based queue "
                 "spaces yet"
              << std::endl;
  }
}

using cmd_fn = std::function<void(const std::vector<std::string> &)>;

const std::map<std::string, cmd_fn> commands = {
    {"help", cmd_help},
    {"?", cmd_help},
    {"echo", cmd_echo},
    {"verbose", cmd_verbose},
    {"v", cmd_verbose},
    {"crdl", cmd_crdl},
    {"dsdl", cmd_dsdl},
    {"lidl", cmd_lidl},
    {"livtoc", [](const auto &a) { cmd_dummy_devices("livtoc", a); }},
    {"llt", [](const auto &a) { cmd_dummy_devices("llt", a); }},
    {"ls", [](const auto &a) { cmd_dummy_devices("ls", a); }},
    {"qspacecreate", cmd_qspacecreate},
    {"qspc", cmd_qspacecreate},
    {"qspacechange", cmd_qspacechange},
    {"qspch", cmd_qspacechange},
    {"qspacedelete", cmd_qspacedelete},
    {"qopen", cmd_qopen},
    {"qclose", cmd_qclose},
    {"qcreate", cmd_qcreate},
    {"qc", cmd_qcreate},
    {"qchange", cmd_qchange},
    {"qdelete", cmd_qdelete},
    {"qinfo", cmd_qinfo},
    {"qi", cmd_qinfo},
    {"qlist", cmd_qlist},
    {"ql", cmd_qlist},
    {"qset", cmd_qset},
    {"qscan", cmd_qscan},
    {"qs", cmd_qscan},
    {"qprint", cmd_qprint},
    {"qdeletemsg", cmd_qdeletemsg},
    {"qchangeprio", cmd_qchangeprio},
    {"qchangequeue", cmd_qchangequeue},
    {"qchangetime", cmd_qchangetime},
    {"qchangeexptime", cmd_qchangeexptime},
    {"qcommittrans",
     [](const auto &a) { cmd_qtrans("qcommittrans", a); }},
    {"qaborttrans", [](const auto &a) { cmd_qtrans("qaborttrans", a); }},
    {"qprinttrans", [](const auto &a) { cmd_qtrans("qprinttrans", a); }},
};

}  // namespace

int main() {
  try {
    fux::qm::qmconfig();
  } catch (const std::exception &e) {
    std::cerr << "qmadmin: " << e.what() << std::endl;
    return -1;
  }

  std::cout << "qmadmin - fuxedo queue administration" << std::endl;

  bool tty = isatty(fileno(stdin));
  std::string line;
  for (;;) {
    if (tty) {
      std::cout << "> " << std::flush;
    }
    if (!std::getline(std::cin, line)) {
      break;
    }
    auto args = tokenize(line);
    if (args.empty()) {
      continue;
    }
    if (echo_mode) {
      std::cout << line << std::endl;
    }
    auto cmd = args[0];
    args.erase(args.begin());
    if (cmd == "quit" || cmd == "q") {
      break;
    }
    auto it = commands.find(cmd);
    if (it == commands.end()) {
      std::cout << "Command " << cmd << " not found, enter help for a list of "
                << "commands" << std::endl;
      continue;
    }
    try {
      it->second(args);
    } catch (const std::exception &e) {
      std::cout << "QMEOS: " << e.what() << std::endl;
    }
  }
  return 0;
}
