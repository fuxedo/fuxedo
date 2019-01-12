#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <unistd.h>

#include <system_error>
#include <thread>
#include <vector>

#include <atmidefs.h>

namespace fux {
namespace ipc {

int seminit(key_t key, int nsems);
void semlock(int semid, int num);
void semunlock(int semid, int num);
void semrm(int semid);

class scoped_semlock {
 public:
  scoped_semlock(int semid, int num) : semid_(semid), num_(num) {
    semlock(semid_, num_);
  }
  ~scoped_semlock() { semunlock(semid_, num_); }
  scoped_semlock(scoped_semlock &&) = default;
  scoped_semlock &operator=(scoped_semlock &&) = default;

 private:
  int semid_;
  int num_;
  scoped_semlock(const scoped_semlock &) = delete;
  scoped_semlock &operator=(const scoped_semlock &) = delete;
};

enum transport : char { queue, file };
enum category : char { application, admin };

struct msgbase {
  long mtype;
  enum transport ttype;
  enum category cat;
};

struct msgfile : msgbase {
  char filename[PATH_MAX];
};

struct msgmem : msgbase {
  char servicename[XATMI_SERVICE_NAME_LENGTH];
  long flags;
  int cd;
  int replyq;
  int rval;
  long rcode;
  char data[0];
};

class msg {
 public:
  msg() { bytes_.resize(sizeof(msgmem)); }
  auto &as_msgfile() { return *reinterpret_cast<msgfile *>(buf()); }
  msgmem *operator->() { return reinterpret_cast<msgmem *>(buf()); }
  char *buf() { return &bytes_[0]; }
  size_t size() { return bytes_.size(); }
  void resize(size_t n) { bytes_.resize(n); }
  void resize_data(size_t n) { bytes_.resize(n + sizeof(msgmem)); }
  size_t size() const { return bytes_.size(); }
  size_t size_data() const { return bytes_.size() - sizeof(msgmem); }

  void set_data(char *data, long len);
  void get_data(char **data);

 private:
  std::vector<char> bytes_;
};

int qcreate();
bool qsend(int msqid, msg &data, int flags);
void qrecv(int msqid, msg &data, long msgtype, int flags);
void qdelete(int msqid);
}  // namespace ipc
}  // namespace fux
