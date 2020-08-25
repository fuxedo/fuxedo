// This file is part of Fuxedo
// Copyright (C) 2018 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <tpadm.h>
#include <xatmi.h>

#include "fux.h"
#include "mib.h"

using fux::fml32buf;

static void manage_domain(const std::string &operation, fml32buf &in,
                          fml32buf &out) {
  auto &domain = getmib().domain();

  out.put(TA_IPCKEY, 0, domain.ipckey);
  out.put(TA_MODEL, 0, "SHM");
  out.put(TA_MASTER, 0, domain.master);
  out.put(TA_MAXSERVERS, 0, domain.maxservers);
  out.put(TA_MAXSERVICES, 0, domain.maxservices);
  out.put(TA_MAXQUEUES, 0, domain.maxqueues);
  out.put(TA_MAXGROUPS, 0, domain.maxgroups);
  out.put(TA_MAXACCESSERS, 0, domain.maxaccessers);
}

static void manage_machine(const std::string &operation, fml32buf &in,
                           fml32buf &out) {
  auto &machine = getmib().mach();

  out.put(TA_PMID, 0, machine.address);
  out.put(TA_LMID, 0, machine.lmid);
  out.put(TA_APPDIR, 0, machine.appdir);
  out.put(TA_TUXCONFIG, 0, machine.tuxconfig);
  out.put(TA_TUXDIR, 0, machine.tuxdir);
  out.put(TA_ULOGPFX, 0, machine.ulogpfx);
  out.put(TA_TLOGDEVICE, 0, machine.tlogdevice);
  out.put(TA_BLOCKTIME, 0, machine.blocktime);
}

static void manage_service(const std::string &operation, fml32buf &in,
                           fml32buf &out) {
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
}

static void manage_svcgrp(const std::string &operation, fml32buf &in,
                          fml32buf &out) {
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
}

static void manage_group(const std::string &operation, fml32buf &in,
                         fml32buf &out) {
  tuxconfig tuxcfg;
  tuxcfg.size = 0;
  tuxcfg.ipckey = 0;
  tuxcfg.maxservers = 1;
  tuxcfg.maxservices = 1;
  tuxcfg.maxgroups = 1;
  tuxcfg.maxqueues = 1;
  mib m(tuxcfg, fux::mib::in_heap());

  auto groups = m.groups();

  if (operation == "GET") {
  } else if (operation == "SET") {
  }
}

int tpadmcall(FBFR32 *inbuf, FBFR32 **outbuf, long flags) {
  fml32buf in(&inbuf);
  fml32buf out(outbuf);

  auto klass = in.get<std::string>(TA_CLASS, 0);
  auto operation = in.get<std::string>(TA_OPERATION, 0);
  if (klass == "T_DOMAIN") {
    manage_domain(operation, in, out);
  } else if (klass == "T_MACHINE") {
    manage_machine(operation, in, out);
  } else if (klass == "T_SERVICE") {
    manage_service(operation, in, out);
  } else if (klass == "T_SVCGRP") {
    manage_svcgrp(operation, in, out);
  } else if (klass == "T_GROUP") {
    manage_group(operation, in, out);
  }
  // TA_OPERATION: GET, GETNEXT, SET
  // TA_CLASS
  // TA_CURSOR
  // TA_OCCURS
  // TA_FLAGS

  return -1;
}
