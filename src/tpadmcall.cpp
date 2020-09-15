// This file is part of Fuxedo
// Copyright (C) 2018 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <tpadm.h>
#include <xatmi.h>

#include "fux.h"
#include "mib.h"

using fux::fml32buf;

static void t_domain_get(fml32buf &in, fml32buf &out) {
  auto &domain = getmib().domain();

  out.put(TA_IPCKEY, 0, domain.ipckey);
  out.put(TA_MODEL, 0, "SHM");
  out.put(TA_MASTER, 0, domain.master);
  out.put(TA_MAXSERVERS, 0, domain.maxservers);
  out.put(TA_MAXSERVICES, 0, domain.maxservices);
  out.put(TA_MAXQUEUES, 0, domain.maxqueues);
  out.put(TA_MAXGROUPS, 0, domain.maxgroups);
  out.put(TA_MAXACCESSERS, 0, domain.maxaccessers);

  out.put(TA_ERROR, 0, TAOK);
  out.put(TA_OCCURS, 0, 1);
}

static void t_machine_get(fml32buf &in, fml32buf &out) {
  auto &machine = getmib().mach();

  out.put(TA_PMID, 0, machine.address);
  out.put(TA_LMID, 0, machine.lmid);
  out.put(TA_APPDIR, 0, machine.appdir);
  out.put(TA_TUXCONFIG, 0, machine.tuxconfig);
  out.put(TA_TUXDIR, 0, machine.tuxdir);
  out.put(TA_ULOGPFX, 0, machine.ulogpfx);
  out.put(TA_TLOGDEVICE, 0, machine.tlogdevice);
  out.put(TA_BLOCKTIME, 0, machine.blocktime);

  out.put(TA_ERROR, 0, TAOK);
  out.put(TA_OCCURS, 0, 1);
}

static void t_service_get(fml32buf &in, fml32buf &out) {
  auto services = getmib().services();
  FLDOCC32 oc = 0;
  for (size_t i = 0; i < services.length(); i++) {
    auto &service = services.at(i);
    if (service.state == state_t::INValid) {
      continue;
    }

    out.put(TA_SERVICENAME, oc, service.servicename);
    out.put(TA_STATE, oc, to_string(service.state));
    out.put(TA_AUTOTRAN, oc, service.autotran);
    out.put(TA_LOAD, oc, service.load);
    out.put(TA_PRIO, oc, service.prio);
    out.put(TA_BLOCKTIME, oc, service.blocktime);
    out.put(TA_SVCTIMEOUT, oc, service.svctimeout);
    out.put(TA_TRANTIME, oc, service.trantime);
    out.put(TA_BUFTYPE, oc, service.buftype);
    out.put(TA_ROUTINGNAME, oc, service.routingname);
    out.put(TA_SIGNATURE_REQUIRED, oc, service.signature_required);
    out.put(TA_ENCRYPTION_REQUIRED, oc, service.encryption_required);
    out.put(TA_BUFTYPECONV, oc, service.buftypeconv);
    out.put(TA_CACHINGNAME, oc, service.cachingname);
    oc++;
  }

  out.put(TA_ERROR, 0, TAOK);
  out.put(TA_OCCURS, 0, oc);
}

static void t_server_set(fml32buf &in, fml32buf &out) {
  FLDOCC32 oc = 0;
  auto srvgrp = in.get<std::string>(TA_SRVGRP, oc);
  auto srvid = in.get<long>(TA_SRVID, oc);
  auto state = to_state(in.get<std::string>(TA_STATE, oc));
  auto rqaddr = "a";

  auto &m = getmib();

  auto group_idx = m.group_index(srvgrp);
  if (group_idx == mib::badoff) {
    throw std::logic_error("Bad group");
  }

  auto server_idx = m.server_index(srvid, group_idx);
  if (server_idx != mib::badoff) {
    if (state == state_t::NEW) {
      throw std::logic_error("Server already exists");
    }
  } else {
    server_idx = m.servers()->len++;
  }
  auto &server = m.servers().at(server_idx);

  server.rqaddr_idx = m.find_queue(rqaddr);
  if (server.rqaddr_idx == mib::badoff) {
    server.rqaddr_idx = m.make_queue(rqaddr);
  }

  server.group_idx = group_idx;
  server.srvid = srvid;
  checked_copy(in.get<std::string>(TA_SERVERNAME, oc), server.servername);
  server.state = state;
  server.basesrvid = in.get(TA_BASESRVID, oc, 0);
  checked_copy(in.get(TA_CLOPT, oc, "-A"), server.clopt);
  checked_copy(in.get(TA_ENVFILE, oc, ""), server.envfile);
  checked_copy(in.get(TA_DEPENDSON, oc, ""), server.dependson);
  server.grace = in.get(TA_GRACE, oc, 86400);
  server.maxgen = in.get(TA_MAXGEN, oc, 1);
  server.min = in.get(TA_MIN, oc, 1);
  server.max = in.get(TA_MAX, oc, 1);
  server.mindispatchthreads = in.get(TA_MINDISPATCHTHREADS, oc, 1);
  server.maxdispatchthreads = in.get(TA_MAXDISPATCHTHREADS, oc, 1);
  server.threadstacksize = in.get(TA_THREADSTACKSIZE, oc, 0);
  server.curdispatchthreads = in.get(TA_CURDISPATCHTHREADS, oc, 0);
  server.hwdispatchthreads = in.get(TA_HWDISPATCHTHREADS, oc, 0);
  server.numdispatchthreads = in.get(TA_NUMDISPATCHTHREADS, oc, 0);
  checked_copy(in.get<std::string>(TA_RCMD, oc), server.rcmd);
  server.restart = to_bool(in.get(TA_RESTART, oc, "N"));
  server.autostart = true;

  m.queues().at(server.rqaddr).servercnt++;
  m.queues().at(server.rqaddr).server_idx = server_idx;

  out.put(TA_ERROR, 0, TAOK);
  out.put(TA_OCCURS, 0, oc);
}

static void t_server_get(fml32buf &in, fml32buf &out) {
  auto &m = getmib();
  auto srv = m.servers();

  FLDOCC32 oc = 0;
  for (size_t i = 0; i < srv.length(); i++) {
    auto &server = srv.at(i);

    out.put(TA_SRVGRP, oc, m.groups().at(server.group_idx).srvgrp);
    out.put(TA_SRVID, oc, server.srvid);
    out.put(TA_SERVERNAME, oc, server.servername);
    out.put(TA_GRPNO, oc, m.groups().at(server.group_idx).grpno);
    out.put(TA_STATE, oc, to_string(server.state));
    out.put(TA_BASESRVID, oc, server.basesrvid);
    out.put(TA_CLOPT, oc, server.clopt);
    out.put(TA_ENVFILE, oc, server.envfile);
    out.put(TA_DEPENDSON, oc, server.dependson);
    out.put(TA_GRACE, oc, server.grace);
    out.put(TA_MAXGEN, oc, server.maxgen);
    out.put(TA_MIN, oc, server.min);
    out.put(TA_MAX, oc, server.max);
    out.put(TA_MINDISPATCHTHREADS, oc, server.mindispatchthreads);
    out.put(TA_MAXDISPATCHTHREADS, oc, server.maxdispatchthreads);
    out.put(TA_THREADSTACKSIZE, oc, server.threadstacksize);
    out.put(TA_CURDISPATCHTHREADS, oc, server.curdispatchthreads);
    out.put(TA_HWDISPATCHTHREADS, oc, server.hwdispatchthreads);
    out.put(TA_NUMDISPATCHTHREADS, oc, server.numdispatchthreads);
    out.put(TA_RCMD, oc, server.rcmd);
    out.put(TA_RESTART, oc, server.restart);

    out.put(TA_LMID, oc, m.mach().lmid);
    oc++;
  }

  out.put(TA_ERROR, 0, TAOK);
  out.put(TA_OCCURS, 0, oc);
}

static void t_svcgrp_get(fml32buf &in, fml32buf &out) {
  auto &m = getmib();
  auto svcgrp = m.advertisements();
  auto srv = m.servers();
  auto grp = m.groups();

  FLDOCC32 oc = 0;
  for (size_t i = 0; i < svcgrp.length(); i++) {
    auto &adv = svcgrp.at(i);
    auto &service = m.services().at(adv.service);
    out.put(TA_SERVICENAME, oc, service.servicename);
    //    out.put(TA_SRVGRP, oc,
    //    m.groups().at(m.find_group(srv.at(adv.server).grpno)).srvgrp);
    out.put(TA_GRPNO, oc, srv.at(adv.server).grpno);
    out.put(TA_STATE, oc, to_string(adv.state));
    out.put(TA_AUTOTRAN, oc, service.autotran);
    out.put(TA_LOAD, oc, service.load);
    out.put(TA_PRIO, oc, service.prio);
    out.put(TA_BLOCKTIME, oc, service.blocktime);
    out.put(TA_SVCTIMEOUT, oc, service.svctimeout);
    out.put(TA_TRANTIME, oc, service.trantime);
    out.put(TA_LMID, oc, m.mach().lmid);
    out.put(TA_RQADDR, oc, m.queues().at(adv.queue).rqaddr);
    out.put(TA_SRVID, oc, srv.at(adv.server).srvid);
    // Function name if possible char[128]
    out.put(TA_SVCRNAM, oc, "?");
    out.put(TA_BUFTYPE, oc, service.buftype);
    out.put(TA_ROUTINGNAME, oc, service.routingname);
    out.put(TA_SVCTYPE, oc, "APP");
    oc++;
  }

  out.put(TA_ERROR, 0, TAOK);
  out.put(TA_OCCURS, 0, oc);
}

static void t_queue_get(fml32buf &in, fml32buf &out) {
  auto &m = getmib();
  auto queue = m.queues();

  FLDOCC32 oc = 0;
  for (size_t i = 0; i < queue.length(); i++) {
    auto &q = queue.at(i);
    auto &s = m.servers().at(q.server_idx);
    out.put(TA_RQADDR, oc, q.rqaddr);
    out.put(TA_SERVERNAME, oc, s.servername);
    out.put(TA_STATE, oc, "ACT");
    out.put(TA_GRACE, oc, s.grace);
    out.put(TA_MAXGEN, oc, s.maxgen);
    out.put(TA_RCMD, oc, s.rcmd);
    out.put(TA_RESTART, oc, s.restart);
    out.put(TA_CONV, oc, to_string(s.conv));
    out.put(TA_LMID, oc, m.mach().lmid);
    out.put(TA_RQID, oc, q.msqid);
    out.put(TA_SERVERCNT, oc, q.servercnt);

    out.put(TA_TOTNQUEUED, oc, 0);
    out.put(TA_TOTWKQUEUED, oc, 0);
    out.put(TA_SOURCE, oc, m.mach().lmid);
    out.put(TA_NQUEUED, oc, 0);
    out.put(TA_WKQUEUED, oc, 0);
    oc++;
  }

  out.put(TA_ERROR, 0, TAOK);
  out.put(TA_OCCURS, 0, oc);
}

static void t_group_get(fml32buf &in, fml32buf &out) {
  tuxconfig tuxcfg;
  tuxcfg.size = 0;
  tuxcfg.ipckey = 0;
  tuxcfg.maxservers = 1;
  tuxcfg.maxservices = 1;
  tuxcfg.maxgroups = 1;
  tuxcfg.maxqueues = 1;
  mib m(tuxcfg, fux::mib::in_heap());

  auto groups = m.groups();
}

static std::map<std::tuple<std::string, std::string>,
                std::function<void(fml32buf &in, fml32buf &out)>>
    implemented = {{{"T_DOMAIN", "GET"}, t_domain_get},
                   {{"T_MACHINE", "GET"}, t_machine_get},
                   {{"T_SERVICE", "GET"}, t_service_get},
                   {{"T_SVCGRP", "GET"}, t_svcgrp_get},
                   {{"T_SERVER", "SET"}, t_server_set},
                   {{"T_SERVER", "GET"}, t_server_get},
                   {{"T_GROUP", "GET"}, t_group_get},
                   {{"T_QUEUE", "GET"}, t_queue_get}};

int tpadmcall(FBFR32 *inbuf, FBFR32 **outbuf, long flags) {
  fml32buf in(&inbuf);
  fml32buf out(outbuf);

  auto klass = in.get<std::string>(TA_CLASS, 0);
  auto operation = in.get<std::string>(TA_OPERATION, 0);

  auto func = implemented.find({klass, operation});
  if (func == implemented.end()) {
  } else {
    func->second(in, out);
  }

  return -1;
}
