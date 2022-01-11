#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <thread>

#include <sys/ipc.h>
#include <sys/shm.h>

#include "ipc.h"
#include "misc.h"
#include "ubb.h"

// https://docs.oracle.com/cd/E72452_01/tuxedo/docs1222/rf5/rf5.html#1803508

struct transaction_table;

struct accesser {
  pid_t pid;
  int rpid;
  std::chrono::steady_clock::time_point rpid_timeout;

  bool valid() const { return pid != 0; }
  void invalidate() {
    pid = 0;
    rpid = -1;
  }
};

struct t_domain {
  uint32_t ipckey;

  uint32_t trantime;
  uint16_t maxservers;
  uint16_t maxservices;
  uint16_t maxqueues;
  uint16_t maxgroups;
  uint16_t maxaccessers;
  uint16_t blocktime;
  uint16_t sanityscan;

  char master[255 + 1];
  char autotran[1 + 1];
  char state[3 + 1];
  char domainid[30 + 1];
  char ldbal[1 + 1];
};

struct t_machine {
  // char address[30];
  // Does not fit Travis CI machine names like
  // travis-job-639f543d-4c99-47ee-b050-1a08446902d8
  char address[64];
  char lmid[30];
  char tuxconfig[256];
  char tuxdir[256];
  char appdir[256];
  char ulogpfx[256];
  char tlogdevice[256];
  long blocktime;
};

struct group {
  uint16_t grpno;
  char srvgrp[30];
  char openinfo[256];
  char closeinfo[256];
  char tmsname[256];
};

enum class state_t : uint8_t {
  INValid = 0,
  MIGrating,
  CLEaning,
  REStarting,
  SUSpended,
  PARtitioned,
  DEAd,
  NEW,
  ACTive,
  INActive,
  undefined
};

struct server {
  uint16_t group_idx;
  uint16_t srvid;
  uint16_t grpno;
  uint16_t mindispatchthreads;
  uint16_t maxdispatchthreads;
  uint16_t curdispatchthreads;
  uint16_t hwdispatchthreads;
  uint16_t numdispatchthreads;
  uint16_t basesrvid;
  uint16_t min;
  uint16_t max;
  bool autostart;
  pid_t pid;
  time_t last_alive_time;
  size_t rqaddr;
  size_t rqaddr_idx;
  state_t state;
  char servername[128];
  char clopt[1024];   // -A
  char envfile[256];  // ""
  char dependson[1024];
  char rcmd[256];
  bool restart;  // "N"

  long grace;  // 86400
  uint16_t maxgen;
  long threadstacksize;
  bool conv;

  void suspend() { state = state_t::SUSpended; }

  bool is_inactive() const { return state == state_t::INActive; }
};

struct queue {
  char rqaddr[32];
  int msqid;
  long mtype;
  long servercnt;

  uint16_t server_idx;
};

struct service {
  char servicename[XATMI_SERVICE_NAME_LENGTH];
  state_t state;
  char autotran[2];
  long load;
  long prio;
  long blocktime;
  long svctimeout;
  long trantime;
  char buftype[256];
  char routingname[16];
  char signature_required[2];
  char encryption_required[2];
  char buftypeconv[10];  // XML2FML, XML2FML32, NOCONVERT
  char cachingname[32];
  uint64_t revision;

  void modified() { revision++; }
};

struct advertisement {
  size_t service;
  size_t queue;
  size_t server;
  state_t state;
};
