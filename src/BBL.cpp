// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <xa.h>
#include <algorithm>
#include <fcntl.h>
#include <spawn.h>
#include <sstream>
#include <sys/wait.h>

#include "fux.h"
#include "mib.h"

#include <atmi.h>
#include <stdio.h>
#include <tpadm.h>
#include <userlog.h>
#include <xa.h>

extern char **environ;

#if defined(__cplusplus)
extern "C" {
#endif
void TMIB(TPSVCINFO *);
int _tmrunserver(int);
extern struct xa_switch_t tmnull_switch;
extern int _tmbuilt_with_thread_option;
#if defined(__cplusplus)
}
#endif

static struct tmdsptchtbl_t _tmdsptchtbl[] = {
    {".TMIB", "TMIB", TMIB, 0, 0, NULL}, {NULL, NULL, NULL, 0, 0, NULL}};
static struct tmsvrargs_t tmsvrargs = {
    &tmnull_switch, _tmdsptchtbl, 0,    tpsvrinit, tpsvrdone, _tmrunserver,
    NULL,           NULL,         NULL, NULL,      tprminit,  tpsvrthrinit,
    tpsvrthrdone,   NULL};

struct tmsvrargs_t *_tmgetsvrargs() {
  return &tmsvrargs;
}

int main(int argc, char **argv) {
  _tmbuilt_with_thread_option = 0;
  return _tmstartserver(argc, argv, _tmgetsvrargs());
}

static void handle_blocktime() {
  mib &m = getmib();
  auto now = std::chrono::steady_clock::now();

  auto accessers = m.accessers();
  for (size_t i = 0; i < accessers->len; i++) {
    auto &acc = accessers[i];
    if (!acc.valid()) {
      continue;
    }
    if (acc.rpid_timeout < now) {
      fux::ipc::msg req;
      req->cat = fux::ipc::unblock;
      userlog("Sending unblock message to client=%d queue=%0x", int(i),
              acc.rpid);
      // the receive side may be gone with its queue stuffed full: send
      // with the cadence-retry window instead of blocking BBL outright
      if (!fux::ipc::qsend(acc.rpid, req, 30000, fux::ipc::flags::noflags)) {
        userlog("Unblock message to client=%d could not be delivered, "
                "queue stayed full for 30s",
                int(i));
      }
    }
  }
}

static void monitor_servers();

// MAXGEN/RESTART: fork+exec a fresh instance of a dead server from the
// watchdog, the way tmboot starts it (server re-registers itself and
// flips its entry ACTive during init).
static void respawn(server &srv, const std::string &path) {
  std::string grpno = std::to_string(srv.grpno);
  std::string srvid = std::to_string(srv.srvid);
  std::vector<const char *> argv = {srv.servername, "-g", grpno.c_str(),
                                    "-i", srvid.c_str(), "-A", nullptr};

  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY,
                                   0);
  posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "stdout",
                                   O_WRONLY | O_CREAT | O_APPEND, 0666);
  posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "stderr",
                                   O_WRONLY | O_CREAT | O_APPEND, 0666);

  std::vector<std::string> env;
  for (char **e = environ; *e != nullptr; e++) {
    env.emplace_back(*e);
  }
  env.push_back("PATH=" + path);
  std::vector<char *> envp_v;
  for (auto &e : env) {
    envp_v.push_back(const_cast<char *>(e.c_str()));
  }
  envp_v.push_back(nullptr);

  pid_t pid = 0;
  int rc = posix_spawnp(&pid, srv.servername, &fa, nullptr,
                        const_cast<char *const *>(&argv[0]), envp_v.data());
  posix_spawn_file_actions_destroy(&fa);
  if (rc != 0) {
    userlog("Unable to restart %s -g %s -i %s: %s", srv.servername,
            grpno.c_str(), srvid.c_str(), strerror(rc));
    return;
  }
  srv.pid = pid;
}

static void maybe_restart(mib &m, server &srv) {
  if (!srv.restart) {
    return;
  }

  time_t now = time(nullptr);
  if (!srv.consume_restart_slot(now)) {
    userlog("Not restarting %s -g %d -i %d: MAXGEN=%d reached within GRACE=%ld",
            srv.servername, srv.grpno, srv.srvid, srv.maxgen, srv.grace);
    return;
  }

  std::ostringstream path;
  path << m.mach().tuxdir << "/bin:" << m.mach().appdir << ":"
       << fux::util::getenv("PATH", "");

  userlog("Restarting %s -g %d -i %d, generation %d", srv.servername,
          srv.grpno, srv.srvid, srv.generation);
  respawn(srv, path.str());
}

static void monitor_servers() {
  mib &m = getmib();
  auto servers = m.servers();

  for (int i = servers->len - 1; i >= 0; i--) {
    auto &server = servers[i];
    if (server.pid == 0) {
      continue;
    }
    if (!alive(server.pid)) {
      // Only an ACTive server is restarted: tmshutdown marks its targets
      // SUSpended and a clean exit leaves INActive, so neither is revived.
      bool was_active = server.state == state_t::ACTive;
      server.pid = 0;
      m.cleanup_server_advertisements(i);
      if (was_active) {
        maybe_restart(m, server);
      }
    }
  }
}

static void monitor_clients() {
  mib &m = getmib();
  auto accessers = m.accessers();

  for (int i = accessers->len - 1; i >= 0; i--) {
    auto &accesser = accessers[i];
    if (accesser.pid == 0) {
      continue;
    }
    if (!alive(accesser.pid)) {
      accesser.pid = 0;
      if (accesser.rpid != 0) {
        fux::ipc::qdelete(accesser.rpid);
        accesser.rpid = 0;
      }
    }
  }
}

// Reap respawned server children; without this they stay zombies and
// kill(pid, 0) keeps reporting them as alive, blocking any restart.
static void reap_children() {
  int status = 0;
  while (waitpid(-1, &status, WNOHANG) > 0) {
    if (WIFSIGNALED(status)) {
      userlog("Server child killed by signal %d", WTERMSIG(status));
    }
  }
}

static void run_watchdog() {
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    reap_children();
    handle_blocktime();
    monitor_servers();
    monitor_clients();
  }
}

int tpsvrinit(int, char *[]) {
  std::thread t(run_watchdog);
  t.detach();
  return 0;
}

extern "C" void TMIB(TPSVCINFO *svcinfo) {
  fux::fml32buf rq(svcinfo);
  fux::fml32buf rs;

  tpadmcall(rq.ptr(), rs.ptrptr(), 0);

  fux::tpreturn(TPSUCCESS, 0, rs);
}
