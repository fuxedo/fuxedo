// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <atmi.h>

#include "ipc.h"
#include "misc.h"
#include "probes.h"

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

namespace fux::ipc {

union semun {
  int val;
  struct semid_ds *buf;
  ushort *array;
};

/*
 * more-than-inspired by W. Richard Stevens' UNIX Network
 * Programming 2nd edition, volume 2, lockvsem.c, page 295.
 */
int seminit(key_t key, int nsems) {
  int semid = semget(key, nsems, IPC_CREAT | IPC_EXCL | 0666);

  if (semid >= 0) { /* we got it first */
    struct sembuf sb;
    sb.sem_op = 1;
    sb.sem_flg = 0;

    for (sb.sem_num = 0; sb.sem_num < nsems; sb.sem_num++) {
      /* do a semop() to "free" the semaphores. */
      /* this sets the sem_otime field, as needed below. */
      if (semop(semid, &sb, 1) == -1) {
        auto e = errno;
        semctl(semid, 0, IPC_RMID); /* clean up */
        throw std::system_error(e, std::generic_category(),
                                "Semaphore creation/init failed");
      }
    }

  } else if (errno == EEXIST) { /* someone else got it first */

    semid = semget(key, nsems, 0); /* get the id */
    if (semid < 0) {
      /* error, check errno */
      throw std::system_error(errno, std::generic_category(),
                              "Semaphore creation/get failed");
    }

    /* wait for other process to initialize the semaphore: */
    union semun arg;
    struct semid_ds buf;
    arg.buf = &buf;
    bool ready = false;
    while (!ready) {
      semctl(semid, nsems - 1, IPC_STAT, arg);
      if (arg.buf->sem_otime != 0) {
        ready = true;
      } else {
        std::this_thread::yield();
      }
    }
  } else {
    throw std::system_error(errno, std::generic_category(),
                            "Semaphore creation failed");
  }

  return semid;
}

static void semop(int semid, struct sembuf sop) {
  if (semop(semid, &sop, 1) == -1) {
    throw std::system_error(errno, std::generic_category(),
                            "Semaphore lock failed");
  }
}

void semlock(int semid, int num) {
  struct sembuf sb;
  sb.sem_num = num;
  sb.sem_op = -1;
  sb.sem_flg = SEM_UNDO;

  semop(semid, sb);
}

void semunlock(int semid, int num) {
  struct sembuf sb;
  sb.sem_num = num;
  sb.sem_op = 1;
  sb.sem_flg = SEM_UNDO;

  semop(semid, sb);
}

void semrm(int semid) {
  union semun arg;
  if (semctl(semid, 0, IPC_RMID, arg) == -1) {
    throw std::system_error(errno, std::generic_category(),
                            "Semaphore removal failed");
  }
}

}  // namespace fux::ipc

namespace fux::ipc {

#define fail_if(cond)                                            \
  if ((cond)) {                                                  \
    throw std::system_error(                                     \
        errno, std::system_category(),                           \
        std::string(__FILE__) + ":" + std::to_string(__LINE__)); \
  }

int qcreate() {
  int msqid = msgget(IPC_PRIVATE, 0600 | IPC_CREAT);
  if (msqid == -1) {
    throw std::system_error(errno, std::system_category(),
                            "Failed to create IPC queue");
  }
  return msqid;
}

static size_t queue_msgmax() {
  static const size_t max = [] {
    std::ifstream proc("/proc/sys/kernel/msgmax");
    size_t value = 0;
    if (proc >> value && value > 0) {
      return value;
    }
    return size_t{8192};
  }();
  return max;
}

static bool msgsnd_timed(int msqid, void *ptr, size_t len, enum flags flag,
                         long msec) {
  if (flag == fux::ipc::notime) {
    int n = msgsnd(msqid, ptr, len - sizeof(long), 0);
    if (n != -1) {
      return true;
    } else {
      throw std::system_error(errno, std::system_category());
    }
  } else if (flag == fux::ipc::noblock) {
    int n = msgsnd(msqid, ptr, len - sizeof(long), IPC_NOWAIT);
    if (n != -1) {
      return true;
    } else {
      if (errno == EAGAIN) {
        return false;
      }
      throw std::system_error(errno, std::system_category());
    }
  } else {  // noflags
    // The platform paces blocking retries once a second; that quantum
    // is what shows up as whole-second latency steps when queues are
    // full, and it compounds hop by hop. Fuxedo paces at 100 ms by
    // default: the API contract is "the send eventually lands or
    // times out", and nothing observes the granularity except the
    // latency. FUX_SEND_CADENCE_MS restores the platform's 1000 for
    // strace-level comparisons.
    static const long interval_msec = [] {
      const char *env = getenv("FUX_SEND_CADENCE_MS");
      long v = env != nullptr ? atol(env) : 100;
      if (v < 1) {
        v = 1;
      } else if (v > 60000) {
        v = 60000;
      }
      return v;
    }();
    int times = msec / interval_msec + 2;

    for (int i = 0; i < times; i++) {
      int n = msgsnd(msqid, ptr, len - sizeof(long), IPC_NOWAIT);
      if (n != -1) {
        return true;
      } else if (errno != EAGAIN) {
        fprintf(stderr, "ERROR %d %s\n", errno, strerror(errno));
        throw std::system_error(errno, std::system_category());
      }
      if (i == 0) {
        // Immediate retry
        continue;
      }

      struct timespec req = {interval_msec / 1000,
                             (interval_msec % 1000) * 1000000};
      if (nanosleep(&req, nullptr) == -1) {
        fprintf(stderr, "!!ERROR %d %s\n", errno, strerror(errno));
        throw std::system_error(errno, std::system_category());
      }
    }
  }
  return false;
}

namespace {
// like the platform: spill files go to $TMPDIR (default /tmp)
void make_spill_name(char *buf, size_t size) {
  const char *tmpdir = getenv("TMPDIR");
  if (tmpdir == nullptr || tmpdir[0] == '\0') {
    tmpdir = "/tmp";
  }
  snprintf(buf, size, "%s/TUXXXXXXX", tmpdir);
}

// Writes the payload to a fresh TUX* file and builds the tiny control
// message that travels the queue instead.
void spill_to_file(fux::ipc::msg &data, msgfile &fmsg, size_t &flen) {
  char tmpname[PATH_MAX];
  make_spill_name(tmpname, sizeof(tmpname));
  int fd = mkstemp(tmpname);
  fail_if(fd == -1);
  fail_if(static_cast<size_t>(write(fd, data.buf() + sizeof(msgbase),
                                    data.size() - sizeof(msgbase))) !=
          data.size() - sizeof(msgbase));
  fail_if(close(fd) == -1);

  fmsg.mtype = data->mtype;
  fmsg.ttype = fux::ipc::file;
  auto namelen = strlen(tmpname) + 1;
  std::copy_n(tmpname, namelen, fmsg.filename);
  flen = sizeof(msgbase) + namelen;
}
}  // namespace

// One send attempt: noblock and noflags probe with IPC_NOWAIT (a full
// queue answers EAGAIN -> false), notime blocks in the kernel.
static bool msgsnd_once(int msqid, void *ptr, size_t len, enum flags flag) {
  int n = flag == fux::ipc::notime
              ? msgsnd(msqid, ptr, len - sizeof(long), 0)
              : msgsnd(msqid, ptr, len - sizeof(long), IPC_NOWAIT);
  if (n != -1) {
    return true;
  }
  if (errno == EAGAIN && flag != fux::ipc::notime) {
    return false;
  }
  fprintf(stderr, "ERROR %d %s\n", errno, strerror(errno));
  throw std::system_error(errno, std::system_category());
}

bool qsend(int msqid, msg &data, long timeout, enum flags flags) {
  FUX_PROBE2(qsend, msqid, static_cast<long>(data.size()));
  if (data.size() - sizeof(long) <= queue_msgmax()) {
    data->ttype = fux::ipc::queue;
    if (msgsnd_once(msqid, data.buf(), data.size(), flags)) {
      return true;
    }
    if (flags == fux::ipc::noblock) {
      // NOBLOCK callers get TPEBLOCK with no spill, like the platform
      return false;
    }
    if (sizeof(long) + data.size() <= 500) {
      // small messages ride out a full queue inline at the ~1s
      // cadence; the platform only spills above 500 bytes total
      return msgsnd_timed(msqid, data.buf(), data.size(), flags, timeout);
    }
  }
  // like the platform: payloads above the kernel per-message limit, and
  // payloads above 500 bytes on a blocking full queue, spill into
  // /tmp/TUX* files; only the filename travels the queue and the
  // control message waits out the queue at the ~1s cadence
  msgfile fmsg;
  size_t flen;
  spill_to_file(data, fmsg, flen);
  if (!msgsnd_timed(msqid, &fmsg, flen, flags, timeout)) {
    unlink(fmsg.filename);
    return false;
  }
  return true;
}

namespace {
void load_from_file(fux::ipc::msg &data, size_t nbytes) {
  if (data->ttype != fux::ipc::file) {
    return;
  }
  char filename[nbytes];
  strcpy(filename, data.as_msgfile().filename);
  struct stat st;
  fail_if(stat(filename, &st) == -1);
  int fd = open(filename, 0);
  fail_if(fd == -1);
  data.resize(sizeof(msgbase) + st.st_size);
  fail_if(read(fd, data.buf() + sizeof(msgbase), st.st_size) != st.st_size);
  fail_if(close(fd) == -1);
  fail_if(unlink(filename) == -1);
}
}  // namespace

// IPC_NOWAIT
void qrecv(int msqid, msg &data, long msgtype, int flags) {
  data.resize(queue_msgmax());

  // MSGMAX; a handled signal must not surface as an error on this
  // blocking path - server idle timers and secondary-queue wakeups rely
  // on that
  ssize_t n;
  do {
    n = msgrcv(msqid, data.buf(), queue_msgmax(), msgtype, flags);
  } while (n == -1 && errno == EINTR);
  if (n == -1) {
    throw std::system_error(errno, std::system_category());
  }
  data.resize(n + sizeof(long));
  FUX_PROBE2(qrecv, msqid, static_cast<long>(data.size()));
  load_from_file(data, n);
}

bool qrecv_intr(int msqid, msg &data, long msgtype) {
  data.resize(queue_msgmax());
  ssize_t n = msgrcv(msqid, data.buf(), queue_msgmax(), msgtype, 0);
  if (n == -1) {
    if (errno == EINTR) {
      return false;
    }
    throw std::system_error(errno, std::system_category());
  }
  data.resize(n + sizeof(long));
  FUX_PROBE2(qrecv, msqid, static_cast<long>(data.size()));
  load_from_file(data, n);
  return true;
}

bool qrecv_wait(int msqid, msg &data, long msgtype, int flags) {
  data.resize(queue_msgmax());
  ssize_t n = msgrcv(msqid, data.buf(), queue_msgmax(), msgtype, flags);
  if (n == -1) {
    if (errno == EINTR) {
      return false;
    }
    throw std::system_error(errno, std::system_category());
  }
  data.resize(n + sizeof(long));
  FUX_PROBE2(qrecv, msqid, static_cast<long>(data.size()));
  load_from_file(data, n);
  return true;
}

void qdelete(int msqid) { msgctl(msqid, IPC_RMID, NULL); }

void msg::set_data(char *data, long len) {
  if (data == nullptr) {
    resize_data(0);
    return;
  }
  auto needed = fux::mem::bufsize(data, len);
  resize_data(needed);
  if (tpexport(data, len, (*this)->data, &needed, 0) == -1) {
    throw std::runtime_error("tpexport failed");
  }
}

void msg::get_data(char **data) {
  if (tpimport((*this)->data, size_data(), data, 0, 0) == -1) {
    throw std::runtime_error("tpimport failed");
  }
}

}  // namespace fux::ipc
