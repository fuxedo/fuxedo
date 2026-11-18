// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <setjmp.h>
#include <deque>
#include <tpadm.h>
#include <userlog.h>
#include <xa.h>
#include <xatmi.h>

#include <atomic>
#include <clara.hpp>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include "fux.h"
#include "svcrepo.h"
#include "compat.h"
#include "ipc.h"
#include "mib.h"
#include "misc.h"
#include "trace.h"
#include "trx.h"

#if defined(__cplusplus)
extern "C" {
#endif
int _tmrunserver(int);
int _tmbuilt_with_thread_option;
int _tmstartserver(int argc, char **argv, struct tmsvrargs_t *tmsvrargs);
#if defined(__cplusplus)
}
#endif

static void dispatch();

int tprminit(char *, void *) { return 0; }
int tpsvrinit(int, char **) {
  if (tpopen() == -1) {
    userlog("tpopen() failed: %s", tpstrerror(tperrno));
    return -1;
  }
  return 0;
}
void tpsvrdone() {
  if (tpclose() == -1) {
    userlog("tpclose() failed: %s", tpstrerror(tperrno));
  }
}
int tpsvrthrinit(int, char **) {
  if (tpopen() == -1) {
    userlog("tpopen() failed: %s", tpstrerror(tperrno));
    return -1;
  }
  return 0;
}
void tpsvrthrdone() {
  if (tpclose() == -1) {
    userlog("tpclose() failed: %s", tpstrerror(tperrno));
  }
}

void ubb2mib(ubbconfig &u, mib &m);

queue_entry get_queue(const char *svc);

struct server_main {
  uint16_t srvid;
  uint16_t grpno;

  int request_queue;

  size_t mib_server;
  size_t mib_queue;

  int argc;
  char **argv;
  struct tmsvrargs_t *tmsvrargs;

  std::mutex mutex;
  std::map<const char *, void (*)(TPSVCINFO *), cmp_cstr> advertisements;

  // SECONDARYRQ handoff: the secondary thread receives and forwards
  // messages here; the main thread is woken with SIGALRM and executes them.
  std::mutex pending_mutex;
  std::deque<fux::ipc::msg> pending;
  int secondary_msqid = -1;
  pid_t main_tid = 0;
  std::thread secondary_thread;
  std::atomic<bool> secondary_stop;
  // dispatch threads increment this right before blocking on the queue;
  // the server is only reported ACTIVE once all of them can receive
  std::atomic<int> threads_ready{0};

  mib &m_;
  std::atomic<bool> stop;

  server_main(mib &m)
      : secondary_stop(false), m_(m), stop(false), req_counter_(0) {
    mtype_ = -(1L << 30);
  }

  void enqueue_pending(fux::ipc::msg &m) {
    std::lock_guard<std::mutex> lock(pending_mutex);
    pending.push_back(std::move(m));
  }

  bool pop_pending(fux::ipc::msg &out) {
    std::lock_guard<std::mutex> lock(pending_mutex);
    if (pending.empty()) {
      return false;
    }
    out = std::move(pending.front());
    pending.pop_front();
    return true;
  }

  void active() { m_.servers().at(mib_server).state = state_t::ACTive; }
  void inactive() { m_.servers().at(mib_server).state = state_t::INActive; }

  int tpadvertisex(const char *svcname, void (*func)(TPSVCINFO *), long flags) {
    // TODO: flags
    fux::scoped_fuxlock lock(mutex);

    if (svcname == nullptr || strlen(svcname) == 0) {
      TPERROR(TPEINVAL, "invalid svcname");
      return -1;
    }
    if (func == nullptr) {
      TPERROR(TPEINVAL, "invalid func");
      return -1;
    }
    if (fux::compat::is221) {
      // 22.x restricts tpadvertise() to names declared with buildserver -s:
      // an exact table match or an extension of a "."-admin entry. A binary
      // with an empty dispatch table is a platform server and is
      // unrestricted.
      auto *rec = tmsvrargs->tmdsptchtbl;
      bool allowed = rec->svcname == nullptr;
      for (; !allowed && rec->svcname != nullptr; rec++) {
        auto len = strlen(rec->svcname);
        if (strcmp(rec->svcname, svcname) == 0 ||
            (rec->svcname[0] == '.' && strncmp(rec->svcname, svcname, len) == 0)) {
          allowed = true;
        }
      }
      if (!allowed) {
        TPERROR(TPEINVAL, "%s was not declared with buildserver -s", svcname);
        return -1;
      }
    }
    /*
       [TPEPROTO]
       tpadvertise() was called in an improper context (for example, by a
       client).
       [TPESYSTEM]
       An Oracle Tuxedo system error has occurred. The exact nature of the error
       is written to a log file.
       [TPEOS]
       An operating system error has occurred.o
       */
    auto it = advertisements.find(svcname);

    if (it != advertisements.end()) {
      if (it->second != func) {
        TPERROR(TPEMATCH, "svcname advertised with a different func");
        return -1;
      }
    } else {
      bool secondary = (flags & TPSECONDARYRQ) != 0;
      try {
        auto lock = m_.data_lock();
        auto queue = mib_queue;
        if (secondary) {
          if (!m_.servers().at(mib_server).secondaryrq) {
            TPERROR(TPEPROTO, "%s not booted with SECONDARYRQ=Y", svcname);
            return -1;
          }
          m_.make_secondary_rqaddr(mib_server);
        }
        if ((flags & TPSINGLETON) != 0 &&
            m_.find_singleton(svcname, mib_queue, mib_server) != mib::badoff) {
          TPERROR(TPENOSINGLETON, "%s already advertised as singleton",
                  svcname);
          return -1;
        }
        m_.advertise(svcname, queue, mib_server, secondary);
      } catch (const std::out_of_range &e) {
        TPERROR(TPELIMIT, "%s", e.what());
        return -1;
      }
      advertisements.insert(std::make_pair(strdup(svcname), func));
    }
    return 0;
  }

  int tpunadvertise(const char *svcname) {
    fux::scoped_fuxlock lock(mutex);
    if (svcname == nullptr || strlen(svcname) == 0) {
      TPERROR(TPEINVAL, "invalid svcname");
      return -1;
    }
    auto it = advertisements.find(svcname);

    if (it != advertisements.end()) {
      auto freeme = it->first;
      advertisements.erase(it);
      free(const_cast<char *>(freeme));
      m_.unadvertise(svcname, mib_queue, mib_server);
    } else {
      TPERROR(TPENOENT, "svcname not advertised");
      return -1;
    }
    return 0;
  }

  bool handle(long mtype, fux::fml32buf &buf) {
    (void)mtype;
    req_counter_ = 0;

    if (buf.get<long>(TA_SRVID, 0) == srvid &&
        buf.get<long>(TA_GRPNO, 0) == grpno) {
      stop = true;
      return true;
    }
    return false;
  }

  void narrow_window() {
    mtype_ = -((1L << 30) - 1);
    req_counter_ = 0;
  }

  long mtype() {
    req_counter_ = (req_counter_ + 1) % 8;
    if (req_counter_ == 0) {
      // every nth message screw priorities and read first message from queue
      return 0;
    }
    return mtype_;
  }

 private:
  long mtype_;
  int req_counter_;
};

static std::unique_ptr<server_main> main_ptr;

namespace fux {
bool is_server() { return main_ptr.get() != nullptr; }
void (*poll_hook)() = nullptr;
}  // namespace fux

int _tmrunserver(int) {
  // Just a placeholder
  return 0;
}

struct server_thread {
  server_thread() : atmibuf(nullptr), reply_to_shutdown(false) {}

  void prepare() {
    if (atmibuf == nullptr) {
      atmibuf = tpalloc(const_cast<char *>("STRING"), nullptr, 4 * 1024);
      fux::mem::setowner(atmibuf, &atmibuf);
    }
  }

  fux::ipc::msg req;
  fux::ipc::msg res;
  char *atmibuf;
  bool reply_to_shutdown;
  jmp_buf tpreturn_env;

  void tpforward(char *svc, char *data, long len, long flags) {
    auto gttid = fux::tx::gttid();
    if (fux::tx::transactional()) {
      fux::tx_end(true);
    }

    auto target = get_queue(svc);
    res.set_data(data, len);

    if (data != nullptr && data != atmibuf) {
      tpfree(data);
    }

    if (flags != 0) {
      userlog("tpforward with flags!=0");
    }
    checked_copy(svc, res->servicename);
    res->flags = flags;
    res->replyq = req->replyq;
    res->mtype = req->cd;
    res->cd = req->cd;
    res->gttid = gttid;

    fux::ipc::qsend(target.msqid, res, 0, fux::ipc::flags::notime);

    longjmp(tpreturn_env, 1);
  }

  void reply(int rval, long rcode, char *data, long len, long flags) {
    if (req->replyq != -1) {
      res.set_data(data, len);

      if (data != nullptr && data != atmibuf) {
        tpfree(data);
      }

      if (flags != 0) {
        userlog("tpreturn with flags!=0");
        res->rval = TPESVCERR;
      } else {
        res->rval = rval;
      }
      res->rcode = rcode;
      res->flags = flags;
      res->mtype = req->cd;
      res->cd = req->cd;

      fux::ipc::qsend(req->replyq, res, 0, fux::ipc::flags::notime);
    }
  }

  void tpreturn(int rval, long rcode, char *data, long len, long flags) {
    if (fux::tx::transactional()) {
      fux::tx_end(rval == TPSUCCESS);
    }
    reply(rval, rcode, data, len, flags);
    longjmp(tpreturn_env, 1);
  }
};

static thread_local std::unique_ptr<server_thread> thread_ptr;
static server_thread *main_thread_ptr;

// Runs one request whose payload has been imported into tp->atmibuf.
// Shared by the primary receive path and the SECONDARYRQ handoff drain;
// requests always execute on this thread (the main dispatch thread).
static void execute(server_thread *tp) {
  TPSVCINFO tpsvcinfo = {};
  checked_copy(tp->req->servicename, tpsvcinfo.name);
  if (main_ptr->advertisements.find(tpsvcinfo.name) ==
      main_ptr->advertisements.end()) {
    // Possible on a shared MSSQ queue when set members do not advertise
    // identical services; cycle the message like Tuxedo does
    tp->req->mtype = 1L << 30;
    main_ptr->narrow_window();
    fux::ipc::qsend(main_ptr->request_queue, tp->req, 0,
                    fux::ipc::flags::notime);
    return;
  }
  tpsvcinfo.data = tp->atmibuf;
  tpsvcinfo.flags = tp->req->flags;
  // the logical length the caller sent, carried by the wire export
  tpsvcinfo.len = static_cast<long>(tp->req.size_data()) -
                  static_cast<long>(fux::mem::headersize());
  tpsvcinfo.cd = tp->req->cd;

  if (tp->req->flags & TPTRAN) {
    fux::tx_join(tp->req->gttid);
  }

  if (setjmp(tp->tpreturn_env) == 0) {
    auto &tracerec = main_ptr->m_.servers().at(main_ptr->mib_server);
    fux::trace::enable(tracerec.tracelvl[0] != '\0');
    fux::trace::event("{ tpservice({\"%s\", %ld, %ld})", tpsvcinfo.name,
                      tpsvcinfo.flags, tpsvcinfo.len);
    auto it = main_ptr->advertisements.find(tpsvcinfo.name);
    it->second(&tpsvcinfo);
  }
  fux::trace::event("} tpservice");
}

static void dispatch() {
  while (true) {
    if (main_ptr->stop) {
      break;
    }

    TPSVCINFO tpsvcinfo;
    bool interrupted = false;
    {
      userlog("Dispatch loop");
      fux::scoped_fuxlock lock(main_ptr->mutex);
      if (main_ptr->stop) {
        break;
      }

      bool queue_gone = false;
      try {
        if (!fux::ipc::qrecv_intr(main_ptr->request_queue, thread_ptr->req,
                                  main_ptr->mtype())) {
          interrupted = true;
        }
      } catch (const std::system_error &e) {
        if (e.code().value() != EIDRM) {
          throw;
        }
        // the queue device was removed (shutdown or tmipcrm): leave
        // quietly instead of taking the whole server down with
        // std::terminate
        queue_gone = true;
      }
      if (queue_gone) {
        break;
      }
      if (interrupted) {
      } else {
        thread_ptr->prepare();
        thread_ptr->req.get_data(&thread_ptr->atmibuf);
        tpsvcinfo.data = thread_ptr->atmibuf;
        if (strcmp(thread_ptr->req->servicename, ".stop") == 0) {
          fux::fml32buf buf(&tpsvcinfo);

          userlog("Received admin message");
          if (main_ptr->handle(thread_ptr->req->mtype, buf)) {
            if (thread_ptr.get() != main_thread_ptr) {
              main_thread_ptr->req = thread_ptr->req;
              main_thread_ptr->req.get_data(&main_thread_ptr->atmibuf);
            }
            main_thread_ptr->reply_to_shutdown = true;
            continue;
          }
          // return for processing by other MSSQ servers: re-type to the
          // maximum and narrow this server's window so it is not seen again
          userlog("Not the target receiver of message, put back in queue");
          thread_ptr->req->mtype = 1L << 30;
          main_ptr->narrow_window();
          fux::ipc::qsend(main_ptr->request_queue, thread_ptr->req, 0,
                          fux::ipc::flags::notime);
          continue;
        }
      }
    }  // lock

    if (interrupted) {
      // a signal arrived while waiting for requests: give an installed
      // hook (e.g. the domain gateway's socket engine) a chance to run
      if (fux::poll_hook) {
        fux::poll_hook();
      }
      // woken by the secondary thread: execute what it handed over, then
      // resume receiving on the primary queue
      fux::ipc::msg handed;
      while (main_ptr->pop_pending(handed)) {
        thread_ptr->req = std::move(handed);
        thread_ptr->prepare();
        thread_ptr->req.get_data(&thread_ptr->atmibuf);
        execute(thread_ptr.get());
      }
      continue;
    }
    execute(thread_ptr.get());
  }
}

// SECONDARYRQ listener: receives on the set's secondary queue, forwards
// each message to the main thread through the in-memory handoff queue and
// wakes it with SIGALRM, then returns to listening immediately.
static void secondary_listen() {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGALRM);
  pthread_sigmask(SIG_BLOCK, &set, nullptr);

  userlog("secondary_listen on msqid %d", main_ptr->secondary_msqid);

  while (!main_ptr->secondary_stop) {
    fux::ipc::msg req;
    try {
      if (!fux::ipc::qrecv_intr(main_ptr->secondary_msqid, req, 0)) {
        continue;
      }
    } catch (const std::system_error &e) {
      if (e.code().value() != EIDRM) {
        throw;
      }
      return;  // secondary queue removed at shutdown
    }
    main_ptr->enqueue_pending(req);
    (void)syscall(SYS_tgkill, getpid(), main_ptr->main_tid, SIGALRM);
  }
}

static void thread_dispatch(tmsvrargs_t *tmsvrargs, int argc, char *argv[]) {
  thread_ptr = std::make_unique<server_thread>();
  if (int n = tmsvrargs->svrthrinit(argc, argv); n != 0) {
    userlog("tpsvrthrinit() = %d", n);
    return;
  }
  main_ptr->threads_ready++;
  dispatch();
  tmsvrargs->svrthrdone();
  thread_ptr.reset();
}

namespace fux::glob {
extern xa_switch_t *xasw;
}

int _tmstartserver(int argc, char **argv, struct tmsvrargs_t *tmsvrargs) {
  bool show_help = false;
  bool verbose = false;

  bool all = false;
  int grpno = -1;
  int srvid = -1;
  std::vector<std::string> services;
  std::string nwaddr;

  auto parser =
      clara::Help(show_help) |
      clara::Opt(srvid, "SRVID")["-i"]("server's SRVID in TUXCONFIG") |
      clara::Opt(grpno, "GRPNO")["-g"]("server's GRPNO in TUXCONFIG") |
      clara::Opt(services, "SERVICES")["-s"]("services to advertise") |
      clara::Opt(nwaddr, "NWADDR")["-n"]("network address (WSL listener)") |
      clara::Opt(all)["-A"]("advertise all services") |
      clara::Opt(verbose)["-v"]("display built-in services");

  int sep = 0;
  for (; sep < argc; sep++) {
    if (strcmp(argv[sep], "--") == 0) {
      break;
    }
  }

  auto result = parser.parse(clara::Args(sep, argv));
  if (!result) {
    std::cerr << parser;
    return -1;
  }
  if (show_help) {
    std::cout << parser;
    return 1;
  }

  if (verbose) {
    struct tmdsptchtbl_t *rec = tmsvrargs->tmdsptchtbl;
    std::cout << "# Service : Function mapping" << std::endl;
    while (rec->svcname != nullptr) {
      std::cout << rec->svcname << ":" << rec->funcname << std::endl;
      rec++;
    }
    return 0;
  }

  if (_tmbuilt_with_thread_option) {
    fux::threaded = true;
  }

  mib &m = getmib();
  main_ptr = std::make_unique<server_main>(m);
  main_ptr->mib_server = m.find_server(srvid, grpno);
  if (main_ptr->mib_server == m.badoff) {
    return -1;
  }

  main_ptr->srvid = srvid;
  main_ptr->grpno = grpno;
  fux::tx::grpno = grpno;

  auto thread_count = m.servers().at(main_ptr->mib_server).mindispatchthreads;
  main_ptr->request_queue = m.make_service_rqaddr(main_ptr->mib_server);
  // REPLYQ queues are created lazily on first use, like the platform
  main_ptr->main_tid = (pid_t)syscall(SYS_gettid);
  main_ptr->mib_queue = m.servers().at(main_ptr->mib_server).rqaddr;
  if (m.servers().at(main_ptr->mib_server).secondaryrq) {
    m.make_secondary_rqaddr(main_ptr->mib_server);
    main_ptr->secondary_msqid =
        m.queues().at(main_ptr->mib_queue).secondary_msqid;

    struct sigaction sa = {};
    sa.sa_handler = +[](int) {};
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, nullptr);

    main_ptr->secondary_thread = std::thread(secondary_listen);
    main_ptr->secondary_thread.detach();
  }
  main_ptr->argc = argc;
  main_ptr->argv = argv;
  main_ptr->tmsvrargs = tmsvrargs;
  fux::glob::xasw = tmsvrargs->xa_switch;

  if (int n = tmsvrargs->svrinit(argc, argv); n != 0) {
    userlog("tpsvrinit() = %d", n);
    return -1;
  }

  thread_ptr = std::make_unique<server_thread>();
  thread_ptr->prepare();
  main_thread_ptr = thread_ptr.get();

  if (all) {
    struct tmdsptchtbl_t *rec = tmsvrargs->tmdsptchtbl;
    while (rec->svcname != nullptr && strlen(rec->svcname) > 0) {
      if (tpadvertise(const_cast<char *>(rec->svcname), rec->svcfunc) == -1) {
        userlog("Failed to advertise service %s / function %s : %s",
                rec->svcname, rec->funcname, tpstrerror(tperrno));
        return -1;
      }
      rec++;
    }
  }

  if (_tmbuilt_with_thread_option && thread_count > 1) {
    std::vector<std::thread> threads;
    for (int i = 0; i < thread_count; i++) {
      threads.emplace_back(std::thread(thread_dispatch, tmsvrargs, argc, argv));
    }
    // become visible as ACTIVE only once every dispatch thread is
    // actually waiting on the request queue, so tmboot does not report
    // a server that cannot serve yet
    for (int i = 0; i < 30000 && main_ptr->threads_ready.load() < thread_count;
         i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    main_ptr->active();

    for (auto &t : threads) {
      t.join();
    }
  } else {
    main_ptr->active();
    dispatch();
  }

  tmsvrargs->mainloop(0);

  tmsvrargs->svrdone();
  main_ptr->inactive();
  if (main_thread_ptr->reply_to_shutdown) {
    main_thread_ptr->reply(TPMINVAL, 0, main_thread_ptr->atmibuf, 0, TPNOFLAGS);
  }
  thread_ptr.reset();
  return 0;
}

int tpadvertise(char *svcname, void (*func)(TPSVCINFO *)) {
  return tpadvertisex(svcname, func, TPNOFLAGS);
}

int tpadvertisex(char *svcname, void (*func)(TPSVCINFO *), long flags) {
  if (!main_ptr) {
    TPERROR(TPEPROTO, "%s can't be called from client", __func__);
    return -1;
  }
  return main_ptr->tpadvertisex(svcname, func, flags);
}

int tpunadvertise(char *svcname) {
  if (!main_ptr) {
    TPERROR(TPEPROTO, "%s can't be called from client", __func__);
    return -1;
  }
  return main_ptr->tpunadvertise(svcname);
}

void tpreturn(int rval, long rcode, char *data, long len, long flags) try {
  if (!main_ptr) {
    TPERROR(TPEPROTO, "%s can't be called from client", __func__);
    abort();
  }
  // trace the service's raw values, before the wire remapping below
  fux::trace::event("{ tpreturn(%d, %ld, %ld)", rval, rcode, len);
  if (rval == TPSUCCESS) {
    rval = TPMINVAL;
  } else if (rval == TPFAIL) {
    rval = TPESVCFAIL;
  } else if (rval == TPEXIT) {
    rval = TPESVCERR;
  } else {
    rval = TPESVCERR;
  }
  return thread_ptr->tpreturn(rval, rcode, data, len, flags);
} catch (...) {
  userlog("tpreturn failed");
  longjmp(thread_ptr->tpreturn_env, -1);
}

void tpforward(char *svc, char *data, long len, long flags) try {
  if (!main_ptr) {
    TPERROR(TPEPROTO, "%s can't be called from client", __func__);
    abort();
  }
  return thread_ptr->tpforward(svc, data, len, flags);
} catch (...) {
  userlog("tpforward failed");
  tpreturn(TPESVCERR, 0, data, len, flags);
}
