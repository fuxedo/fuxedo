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
#include "probes.h"
#include "trx.h"

#if defined(__cplusplus)
extern "C" {
#endif
int _tmrunserver(int);
int _tmbuilt_with_thread_option;
int _tmstartserver(int argc, char **argv, struct tmsvrargs_t *tmsvrargs);
extern struct xa_switch_t tmnull_switch;
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
            TPERROR(TPENOSECONDARYRQ, "%s not booted with SECONDARYRQ=Y",
                    svcname);
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

  // import the request payload, or mark the request bufferless: the
  // service then sees svcinfo->data == NULL and len 0 (pinned against
  // Oracle 12.2.2)
  void load_request() {
    if (static_cast<long>(req.size_data()) > fux::mem::headersize()) {
      req.get_data(&atmibuf);
    } else {
      atmibuf = nullptr;
    }
  }
  bool reply_to_shutdown;
  jmp_buf tpreturn_env;

  // conversational context for the service this thread is currently
  // executing; set up by execute() when the request arrived with TPCONV
  bool conv = false;
  bool conv_open = false;
  bool conv_send_mine = false;

  void tprecv(int cd, char **data, long *len, long flags, long *revent) {
    if (!conv || !conv_open || cd != req->cd) {
      TPERROR(TPEBADDESC, "Unknown or closed conversation %d", cd);
      throw fux::xatmi_error();
    }
    fux::ipc::msg m;
    try {
      fux::ipc::qrecv(main_ptr->request_queue, m, fux::ipc::conv_mtype(cd),
                      (flags & TPNOBLOCK) ? IPC_NOWAIT : 0);
    } catch (const std::system_error &) {
      TPERROR(TPEBLOCK, "No message available");
      throw fux::xatmi_error();
    }
    if (m->revent != 0) {
      *revent = m->revent;
      if (m->revent & TPEV_SENDONLY) {
        conv_send_mine = true;
      }
      if (m->revent &
          (TPEV_SVCSUCC | TPEV_SVCFAIL | TPEV_SVCERR | TPEV_DISCONIMM)) {
        conv_open = false;
      }
    }
    if (static_cast<long>(m.size_data()) > fux::mem::headersize()) {
      long olen = 0;
      if (tpimport(m->data, m.size_data(), data, &olen, 0) == -1) {
        throw fux::xatmi_error();
      }
      if (len != nullptr) {
        // tpimport reports its own bookkeeping length; the caller gets
        // the logical data length, like tpgetrply does
        *len = std::max(0L, static_cast<long>(m.size_data()) -
                                fux::mem::headersize());
      }
    } else if (len != nullptr) {
      *len = 0;
    }
    fux::atmi::reset_tperrno();
  }

  void tpsend(int cd, char *data, long len, long flags, long *revent) {
    if (!conv || !conv_open || cd != req->cd) {
      TPERROR(TPEBADDESC, "Unknown or closed conversation %d", cd);
      throw fux::xatmi_error();
    }
    if (!conv_send_mine) {
      TPERROR(TPEPROTO, "No send permission on conversation %d", cd);
      throw fux::xatmi_error();
    }
    // the service keeps ownership of its buffer; unlike tpreturn the
    // conversation continues after a send
    res.set_data(data, len);
    res->mtype = cd;
    res->cat = fux::ipc::application;
    res->cd = cd;
    res->revent = (flags & TPRECVONLY) ? TPEV_SENDONLY : 0;
    res->replyq = -1;
    if (!fux::ipc::qsend(req->replyq, res, 0, fux::ipc::flags::notime)) {
      TPERROR(TPEOS, "Cannot send on conversation %d: %s", cd,
              strerror(errno));
      throw fux::xatmi_error();
    }
    if (flags & TPRECVONLY) {
      conv_send_mine = false;
    }
    if (revent != nullptr) {
      *revent = 0;
    }
    fux::atmi::reset_tperrno();
  }

  void tpdiscon(int cd) {
    if (!conv || !conv_open || cd != req->cd) {
      TPERROR(TPEBADDESC, "Unknown or closed conversation %d", cd);
      throw fux::xatmi_error();
    }
    res.resize_data(0);
    res->mtype = cd;
    res->cat = fux::ipc::application;
    res->cd = cd;
    res->revent = TPEV_DISCONIMM;
    res->replyq = -1;
    fux::ipc::qsend(req->replyq, res, 0, fux::ipc::flags::notime);
    conv_open = false;
    fux::atmi::reset_tperrno();
  }

  void tpforward(char *svc, char *data, long len, long flags) {
    auto gttid = fux::tx::gttid();
    // TPNOTRAN: the target must not join the caller's transaction
    const bool carry_tx = fux::tx::transactional() && !(flags & TPNOTRAN);
    if (fux::tx::transactional()) {
      fux::tx_end(true);
    }

    auto target = get_queue(svc);
    res.set_data(data, len);

    if (data != nullptr && data != atmibuf) {
      tpfree(data);
    }

    checked_copy(svc, res->servicename);
    res->flags = carry_tx ? TPTRAN : 0;
    res->replyq = (flags & TPNOREPLY) ? -1 : req->replyq;
    res->mtype = req->cd;
    res->cd = req->cd;
    res->gttid = gttid;

    if ((flags & TPNOREPLY) && req->replyq != -1) {
      // the caller must not block on a reply that can never come: the
      // platform releases it with an error as soon as the forward is made
      fux::ipc::msg noreply;
      noreply->mtype = req->cd;
      noreply->cat = fux::ipc::application;
      noreply->rval = TPESVCERR;
      noreply->rcode = 0;
      noreply->cd = req->cd;
      noreply.set_data(nullptr, 0);
      fux::ipc::qsend(req->replyq, noreply, 0, fux::ipc::flags::notime);
    }

    auto sendflags = (flags & TPNOBLOCK) ? fux::ipc::flags::noblock
                                         : fux::ipc::flags::notime;
    fux::ipc::qsend(target.msqid, res, 0, sendflags);

    longjmp(tpreturn_env, 1);
  }

  void reply(int rval, long rcode, char *data, long len, long flags) {
    if (conv) {
      // a conversational return ends the conversation: the terminal
      // event rides to the caller with the return data, if any
      conv_open = false;
      if (req->replyq != -1) {
        res.set_data(data, len);
        if (data != nullptr && data != atmibuf) {
          tpfree(data);
        }
        // tpreturn already remapped the raw service return: TPMINVAL
        // is success, TPESVCFAIL is failure, anything else an error
        long ev = TPEV_SVCERR;
        if (rval == TPMINVAL) {
          ev = TPEV_SVCSUCC;
        } else if (rval == TPESVCFAIL) {
          ev = TPEV_SVCFAIL;
        }
        res->revent = ev;
        res->rcode = rcode;
        res->flags = 0;
        res->mtype = req->cd;
        res->cd = req->cd;
        res->replyq = -1;
        fux::ipc::qsend(req->replyq, res, 0, fux::ipc::flags::notime);
      }
      return;
    }
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
    if (fux::tx::transactional() && fux::tx::own_begin()) {
      // the service began its own transaction and left it open at
      // return: the platform aborts it and tells the caller TPESVCERR
      // (pinned against Oracle 12.2.2)
      ::tx_rollback();
      fux::tx::clear_own_begin();
      rval = TPESVCERR;
    } else if (fux::tx::transactional()) {
      // AUTOTRAN (or the caller's transaction left open): end the
      // branch, then drive the two-phase outcome the way tx_commit
      // does - xa_end alone leaves the transaction uncommitted. Only
      // a transaction this server began itself is driven here; one
      // joined from the caller is committed by the caller's tpcommit
      bool ok = fux::tx_end(rval == TPMINVAL);
      if (ok && rval == TPMINVAL && fux::tx::own_begin()) {
        auto xid = fux::tx::current_xid();
        int drc = fux::tm::drive(&xid);
        ok = drc != 0;
      }
      if (!ok) {
        // the transaction outcome failed (commit aborted / rollback
        // error): the caller learns the transaction did not succeed
        rval = TPEABORT;
      }
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
    tp->req->mtype = (1L << 30) - 1;
    main_ptr->narrow_window();
    fux::ipc::qsend(main_ptr->request_queue, tp->req, 0,
                    fux::ipc::flags::notime);
    return;
  }
  tpsvcinfo.data = tp->atmibuf;
  tpsvcinfo.flags = tp->req->flags;
  // the logical length the caller sent, carried by the wire export;
  // a bufferless request has none
  tpsvcinfo.len = std::max(
      0L, static_cast<long>(tp->req.size_data()) -
              static_cast<long>(fux::mem::headersize()));
  tpsvcinfo.cd = tp->req->cd;
  tpsvcinfo.cltid.clientdata[0] = tp->req->caller;

  if (tp->req->flags & TPTRAN) {
    fux::tx_join(tp->req->gttid);
  }

  // AUTOTRAN=Y: a caller-less request starts its own transaction; the
  // outcome rides on tpreturn (commit on TPSUCCESS, abort otherwise),
  // and an aborted one surfaces as TPESVCFAIL to the caller
  if (!fux::tx::transactional()) {
    auto sidx = main_ptr->m_.find_service(tpsvcinfo.name);
    if (sidx != mib::badoff &&
        main_ptr->m_.services().at(sidx).autotran[0] == 'Y') {
      tx_begin();
      // autotran transactions are platform-begun, not service-begun
      fux::tx::clear_own_begin();
    }
  }

  // conversational requests set up the half-duplex context: the caller
  // holds send permission unless it connected with TPRECVONLY
  tp->conv = (tp->req->flags & TPCONV) != 0;
  if (tp->conv) {
    tp->conv_open = true;
    tp->conv_send_mine = (tp->req->flags & TPRECVONLY) != 0;
  }

  if (setjmp(tp->tpreturn_env) == 0) {
    auto &tracerec = main_ptr->m_.servers().at(main_ptr->mib_server);
    fux::trace::enable(tracerec.tracelvl[0] != '\0');
    fux::trace::event("{ tpservice({\"%s\", %ld, %ld})", tpsvcinfo.name,
                      tpsvcinfo.flags, tpsvcinfo.len);
    auto it = main_ptr->advertisements.find(tpsvcinfo.name);
    FUX_PROBE3(service__entry, tpsvcinfo.name, tpsvcinfo.len, tpsvcinfo.flags);
    fux::atmi::set_dispatch_prio(tp->req->priority);
    it->second(&tpsvcinfo);
    fux::atmi::set_dispatch_prio(-1);
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
        thread_ptr->load_request();
        tpsvcinfo.data = thread_ptr->atmibuf;
        if (strcmp(thread_ptr->req->servicename, ".stop") == 0) {
          fux::fml32buf buf(&tpsvcinfo);

          userlog("Received admin message");
          if (main_ptr->handle(thread_ptr->req->mtype, buf)) {
            if (thread_ptr.get() != main_thread_ptr) {
              main_thread_ptr->req = thread_ptr->req;
              main_thread_ptr->load_request();
            }
            main_thread_ptr->reply_to_shutdown = true;
            continue;
          }
          // return for processing by other MSSQ servers: re-type to the
          // maximum and narrow this server's window so it is not seen again
          userlog("Not the target receiver of message, put back in queue");
          thread_ptr->req->mtype = (1L << 30) - 1;
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
        thread_ptr->load_request();
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
  // a NULL switch means "no resource manager" (system servers ship NULL
  // in their dispatch tables); alias it to the null switch so the
  // transaction machinery sees a plain non-RM server
  fux::glob::xasw = tmsvrargs->xa_switch != nullptr ? tmsvrargs->xa_switch
                                                    : &tmnull_switch;

  // the service init's getopt(3) loop starts at the application
  // argument region: just past the "--" separator, matching the
  // platform (its server main consumes framework options first and
  // leaves optind there; see src/getopt.cpp for the POSIX flavor the
  // platform's server library interposes)
  optind = sep < argc && strcmp(argv[sep], "--") == 0 ? sep + 1 : 1;

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

  // CLOPT "-s NAME" (without -A): advertise exactly the named services,
  // looked up in the dispatch table the way the platform's generated
  // main does; names already advertised via -A are skipped
  for (auto &name : services) {
    struct tmdsptchtbl_t *rec = tmsvrargs->tmdsptchtbl;
    bool found = false;
    while (rec->svcname != nullptr && strlen(rec->svcname) > 0) {
      if (name == rec->svcname) {
        if (main_ptr->advertisements.find(rec->svcname) ==
            main_ptr->advertisements.end()) {
          if (tpadvertise(const_cast<char *>(rec->svcname), rec->svcfunc) ==
              -1) {
            userlog("Failed to advertise service %s / function %s : %s",
                    rec->svcname, rec->funcname, tpstrerror(tperrno));
            return -1;
          }
        }
        found = true;
        break;
      }
      rec++;
    }
    if (!found) {
      // not every -s name comes from the buildserver table: servers
      // like TMQUEUE parse their own -s in tpsvrinit. The generated
      // main only advertises what the table knows.
      userlog("CLOPT service %s not in the dispatch table, leaving it to "
              "tpsvrinit",
              name.c_str());
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
  FUX_PROBE3(tpreturn, rval, rcode, len);
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

int tpdiscon(int cd) try {
  if (main_ptr && thread_ptr && thread_ptr->conv) {
    thread_ptr->tpdiscon(cd);
    return 0;
  }
  return fux::clnt::tpdiscon(cd);
} catch (...) {
  // tperrno carries the failure; the service decides how to wind down
  return -1;
}

int tpsend(int cd, char *data, long len, long flags, long *revent) try {
  if (main_ptr && thread_ptr && thread_ptr->conv) {
    constexpr long valid = TPSIGRSTRT | TPNOBLOCK | TPNOTIME | TPRECVONLY;
    if (flags & ~valid) {
      TPERROR(TPEINVAL, "Invalid flags passed to tpsend");
      return -1;
    }
    thread_ptr->tpsend(cd, data, len, flags, revent);
    return 0;
  }
  return fux::clnt::tpsend(cd, data, len, flags, revent);
} catch (...) {
  return -1;
}

int tprecv(int cd, char **data, long *len, long flags, long *revent) try {
  if (main_ptr && thread_ptr && thread_ptr->conv) {
    if (revent == nullptr) {
      TPERROR(TPEINVAL, "revent is null");
      return -1;
    }
    // client-mask parity: TPNOCHANGE is honored on receive; TPACK is
    // rejected by the platform (pinned 12.2.2)
    constexpr long valid = TPSIGRSTRT | TPNOBLOCK | TPNOTIME | TPNOCHANGE;
    if (flags & ~valid) {
      TPERROR(TPEINVAL, "Invalid flags passed to tprecv");
      return -1;
    }
    thread_ptr->tprecv(cd, data, len, flags, revent);
    return 0;
  }
  return fux::clnt::tprecv(cd, data, len, flags, revent);
} catch (...) {
  return -1;
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
