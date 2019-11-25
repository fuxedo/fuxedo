// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <atmi.h>

#include "ipc.h"
#include "misc.h"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
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

void semlock(int semid, int num) {
  struct sembuf sb;
  sb.sem_num = num;
  sb.sem_op = -1;
  sb.sem_flg = SEM_UNDO;

  if (semop(semid, &sb, 1) == -1) {
    throw std::system_error(errno, std::generic_category(),
                            "Semaphore lock failed");
  }
}

void semunlock(int semid, int num) {
  struct sembuf sb;
  sb.sem_num = num;
  sb.sem_op = 1;
  sb.sem_flg = SEM_UNDO;

  if (semop(semid, &sb, 1) == -1) {
    throw std::system_error(errno, std::generic_category(),
                            "Semaphore unlock failed");
  }
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

static const size_t MAX_QUEUE_MSG_SIZE = 4000;

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
    const long interval_msec = 100;
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

      struct timespec req = {0, interval_msec * 1000000};
      if (nanosleep(&req, nullptr) == -1) {
        fprintf(stderr, "!!ERROR %d %s\n", errno, strerror(errno));
        throw std::system_error(errno, std::system_category());
      }
    }
  }
  return false;
}

bool qsend(int msqid, msg &data, long timeout, enum flags flags) {
  if (data.size() > MAX_QUEUE_MSG_SIZE) {
    char tmpname[] = "/tmp/msgbase-XXXXXX";
    int fd = mkstemp(tmpname);
    fail_if(fd == -1);
    fail_if(write(fd, data.buf() + sizeof(msgbase),
                  data.size() - sizeof(msgbase)) !=
            data.size() - sizeof(msgbase));
    fail_if(close(fd) == -1);

    msgfile fmsg;
    fmsg.mtype = data->mtype;
    fmsg.ttype = fux::ipc::file;
    std::copy_n(tmpname, sizeof(tmpname), fmsg.filename);
    auto len = sizeof(msgbase) + sizeof(tmpname);

    if (!msgsnd_timed(msqid, &fmsg, len, flags, timeout)) {
      unlink(tmpname);
      return false;
    }
    return true;
  } else {
    data->ttype = fux::ipc::queue;
    return msgsnd_timed(msqid, data.buf(), data.size(), flags, timeout);
  }
}

// IPC_NOWAIT
void qrecv(int msqid, msg &data, long msgtype, int flags) {
  data.resize(MAX_QUEUE_MSG_SIZE);

  // MSGMAX
  ssize_t n = msgrcv(msqid, data.buf(), MAX_QUEUE_MSG_SIZE, msgtype, flags);
  if (n == -1) {
    throw std::system_error(errno, std::system_category());
  }
  data.resize(n + sizeof(long));
  if (data->ttype == fux::ipc::file) {
    char filename[n];
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
