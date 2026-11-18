// This file is part of Fuxedo
// Copyright (C) 2018 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <tpadm.h>
#include <xatmi.h>

#include <unistd.h>

#include <cstring>
#include <limits.h>
#include <fstream>

#ifdef HAVE_CXX_FILESYSTEM
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif

#include "cfg.h"
#include "fux.h"
#include "mib.h"
#include "misc.h"

using fux::fml32buf;

namespace {

constexpr const char *STATE_NEW = "NEW";
constexpr const char *STATE_INA = "INA";

fux::cfg::tables load(bool *exists) {
  auto path = fux::util::getenv("TUXCONFIG", "");
  if (path.empty()) {
    throw std::runtime_error("TUXCONFIG environment variable required");
  }
  if (!fs::exists(path)) {
    *exists = false;
    return {};
  }
  *exists = true;
  fux::cfg::tables tables;
  getconfig(&tables);
  return tables;
}

void save(const fux::cfg::tables &tables) {
  auto path = fux::util::getenv("TUXCONFIG", "");
  auto text = fux::cfg::emit(tables);

  tuxconfig hdr = {};
  if (fs::exists(path)) {
    std::ifstream fin(path, std::ios::binary);
    fin.read(reinterpret_cast<char *>(&hdr), sizeof(hdr));
    hdr.revision++;
  } else {
    memcpy(hdr.magic, TUXCONFIG_MAGIC, sizeof(hdr.magic));
    hdr.hdr_size = sizeof(hdr);
    hdr.version = TUXCONFIG_VERSION;
    hdr.revision = 1;
  }
  hdr.text_off = sizeof(hdr);
  hdr.text_size = text.size();
  hdr.ipckey = tables.dom.ipckey;
  hdr.flags = TUXCONFIG_FLAG_LOADED;

  std::ofstream fout(path, std::ios::binary | std::ios::trunc);
  fout.write(reinterpret_cast<char *>(&hdr), sizeof(hdr));
  fout.write(text.data(), text.size());
}

bool is_new(fml32buf &in) {
  return in.get(TA_STATE, 0, "") == STATE_NEW;
}

void get_domain(const fux::cfg::tables &t, fml32buf &out) {
  out.put(TA_IPCKEY, 0, (long)t.dom.ipckey);
  out.put(TA_MASTER, 0, t.dom.master);
  out.put(TA_MODEL, 0, t.dom.model);
  if (!t.dom.domainid.empty()) {
    out.put(TA_DOMAINID, 0, t.dom.domainid);
  }
  out.put(TA_LDBAL, 0, t.dom.ldbal);
  out.put(TA_MAXACCESSERS, 0, (long)t.dom.maxaccessers);
  out.put(TA_MAXSERVERS, 0, (long)t.dom.maxservers);
  out.put(TA_MAXSERVICES, 0, (long)t.dom.maxservices);
  out.put(TA_MAXGROUPS, 0, (long)t.dom.maxgroups);
  out.put(TA_MAXQUEUES, 0, (long)t.dom.maxqueues);
  out.put(TA_STATE, 0, STATE_INA);
}

void get_machine(const fux::cfg::tables &t, fml32buf &out) {
  out.put(TA_PMID, 0, t.mach.pmid);
  out.put(TA_LMID, 0, t.mach.lmid);
  out.put(TA_APPDIR, 0, t.mach.appdir);
  out.put(TA_TUXCONFIG, 0, t.mach.tuxconfig);
  out.put(TA_TUXDIR, 0, t.mach.tuxdir);
  if (!t.mach.ulogpfx.empty()) {
    out.put(TA_ULOGPFX, 0, t.mach.ulogpfx);
  }
  if (!t.mach.tlogdevice.empty()) {
    out.put(TA_TLOGDEVICE, 0, t.mach.tlogdevice);
  }
  out.put(TA_STATE, 0, STATE_INA);
}

void get_group(const fux::cfg::group &g, FLDOCC32 oc, fml32buf &out) {
  out.put(TA_SRVGRP, oc, g.srvgrp);
  out.put(TA_GRPNO, oc, (long)g.grpno);
  out.put(TA_LMID, oc, g.lmid);
  if (!g.openinfo.empty()) {
    out.put(TA_OPENINFO, oc, g.openinfo);
  }
  if (!g.closeinfo.empty()) {
    out.put(TA_CLOSEINFO, oc, g.closeinfo);
  }
  if (!g.tmsname.empty()) {
    out.put(TA_TMSNAME, oc, g.tmsname);
  }
  out.put(TA_STATE, oc, STATE_INA);
}

void get_server(const fux::cfg::server &s, FLDOCC32 oc, fml32buf &out) {
  out.put(TA_SRVGRP, oc, s.srvgrp);
  out.put(TA_SRVID, oc, (long)s.srvid);
  out.put(TA_SERVERNAME, oc, s.servername);
  out.put(TA_MIN, oc, (long)s.min);
  out.put(TA_MAX, oc, (long)s.max);
  out.put(TA_MINDISPATCHTHREADS, oc, (long)s.mindispatchthreads);
  out.put(TA_MAXDISPATCHTHREADS, oc, (long)s.maxdispatchthreads);
  out.put(TA_CLOPT, oc, s.clopt);
  out.put(TA_RQADDR, oc, s.rqaddr);
  out.put(TA_REPLYQ, oc, s.replyq ? "Y" : "N");
  if (s.secondaryrq) {
    out.put(TA_SECONDARYRQ, oc, "Y");
  }
  out.put(TA_MAXGEN, oc, (long)s.maxgen);
  out.put(TA_RESTART, oc, s.restart ? "Y" : "N");
  out.put(TA_GRACE, oc, s.grace);
  if (!s.envfile.empty()) {
    out.put(TA_ENVFILE, oc, s.envfile);
  }
  if (!s.dependson.empty()) {
    out.put(TA_DEPENDSON, oc, s.dependson);
  }
  if (!s.rcmd.empty()) {
    out.put(TA_RCMD, oc, s.rcmd);
  }
  if (s.threadstacksize != 0) {
    out.put(TA_THREADSTACKSIZE, oc, s.threadstacksize);
  }
  out.put(TA_STATE, oc, STATE_INA);
}

size_t cursor(fml32buf &in) {
  auto c = in.get<std::string>(TA_CURSOR, 0, "0");
  try {
    return std::stoul(c);
  } catch (...) {
    return 0;
  }
}

void getnext(const fux::cfg::tables &t, const std::string &klass, fml32buf &in,
             fml32buf &out) {
  auto at = cursor(in);
  if (klass == "T_GROUP") {
    if (at < t.groups.size()) {
      get_group(t.groups[at], 0, out);
      out.put(TA_CURSOR, 0, std::to_string(at + 1).c_str());
      out.put(TA_OCCURS, 0, 1L);
      return;
    }
  } else if (klass == "T_SERVER") {
    if (at < t.servers.size()) {
      get_server(t.servers[at], 0, out);
      out.put(TA_CURSOR, 0, std::to_string(at + 1).c_str());
      out.put(TA_OCCURS, 0, 1L);
      return;
    }
  } else {
    throw std::logic_error("GETNEXT not supported for " + klass);
  }
  out.put(TA_OCCURS, 0, 0L);
}

void set_domain(fux::cfg::tables &t, bool exists, fml32buf &in, fml32buf &out) {
  if (!exists) {
    if (!is_new(in)) {
      throw std::logic_error(
          "T_DOMAIN does not exist, TA_STATE=NEW required to create");
    }
    auto master = in.get(TA_MASTER, 0, "");
    if (master.empty()) {
      throw std::logic_error("TA_MASTER required");
    }
    t.dom.master = master;
    t.dom.ipckey = in.get<long>(TA_IPCKEY, 0, 0);
    if (t.dom.ipckey == 0) {
      throw std::logic_error("TA_IPCKEY required");
    }
    t.mach.lmid = master;
    char host[256] = {0};
    gethostname(host, sizeof(host) - 1);
    t.mach.pmid = host;
    t.mach.tuxconfig = fux::util::getenv("TUXCONFIG", "");
    t.mach.tuxdir = fux::util::getenv("TUXDIR", "");
    t.mach.appdir = in.get(TA_APPDIR, 0, "");
    if (t.mach.appdir.empty()) {
      char cwd[PATH_MAX] = {0};
      getcwd(cwd, sizeof(cwd) - 1);
      t.mach.appdir = cwd;
    }
  } else if (is_new(in)) {
    throw std::logic_error("T_DOMAIN already exists");
  }

  t.dom.model = in.get<std::string>(TA_MODEL, 0, t.dom.model);
  t.dom.domainid = in.get<std::string>(TA_DOMAINID, 0, t.dom.domainid);
  t.dom.ldbal = in.get<std::string>(TA_LDBAL, 0, t.dom.ldbal);
  if (in.get<long>(TA_MAXSERVERS, 0, 0) > 0) {
    t.dom.maxservers = in.get<long>(TA_MAXSERVERS, 0, t.dom.maxservers);
  }
  if (in.get<long>(TA_MAXSERVICES, 0, 0) > 0) {
    t.dom.maxservices = in.get<long>(TA_MAXSERVICES, 0, t.dom.maxservices);
  }
  if (in.get<long>(TA_MAXGROUPS, 0, 0) > 0) {
    t.dom.maxgroups = in.get<long>(TA_MAXGROUPS, 0, t.dom.maxgroups);
  }
  if (in.get<long>(TA_MAXQUEUES, 0, 0) > 0) {
    t.dom.maxqueues = in.get<long>(TA_MAXQUEUES, 0, t.dom.maxqueues);
  }
  if (in.get<long>(TA_MAXACCESSERS, 0, 0) > 0) {
    t.dom.maxaccessers = in.get<long>(TA_MAXACCESSERS, 0, t.dom.maxaccessers);
  }

  out.put(TA_STATE, 0, STATE_INA);
}

void set_group(fux::cfg::tables &t, fml32buf &in, fml32buf &out) {
  auto srvgrp = in.get(TA_SRVGRP, 0, "");
  if (srvgrp.empty()) {
    throw std::logic_error("TA_SRVGRP required");
  }
  auto it = std::find_if(t.groups.begin(), t.groups.end(), [&](auto &g) {
    return g.srvgrp == srvgrp;
  });
  bool creating = it == t.groups.end();
  if (creating) {
    if (!is_new(in)) {
      throw std::logic_error(
          "T_GROUP does not exist, TA_STATE=NEW required to create");
    }
    fux::cfg::group g;
    g.srvgrp = srvgrp;
    g.grpno = in.get<long>(TA_GRPNO, 0, 0);
    if (g.grpno == 0) {
      throw std::logic_error("TA_GRPNO required");
    }
    g.lmid = in.get<std::string>(TA_LMID, 0, t.dom.master);
    g.openinfo = in.get<std::string>(TA_OPENINFO, 0, std::string());
    g.closeinfo = in.get<std::string>(TA_CLOSEINFO, 0, std::string());
    g.tmsname = in.get<std::string>(TA_TMSNAME, 0, std::string());
    t.groups.push_back(std::move(g));
  } else {
    if (is_new(in)) {
      throw std::logic_error("T_GROUP already exists");
    }
    auto inv = in.get(TA_STATE, 0, "");
    if (inv == "INV") {
      t.groups.erase(it);
      return;
    }
    if (in.get<long>(TA_GRPNO, 0, 0) > 0) {
      it->grpno = in.get<long>(TA_GRPNO, 0, it->grpno);
    }
    it->lmid = in.get<std::string>(TA_LMID, 0, it->lmid);
    it->openinfo = in.get<std::string>(TA_OPENINFO, 0, it->openinfo);
    it->closeinfo = in.get<std::string>(TA_CLOSEINFO, 0, it->closeinfo);
    it->tmsname = in.get<std::string>(TA_TMSNAME, 0, it->tmsname);
  }
}

void set_server(fux::cfg::tables &t, fml32buf &in, fml32buf &out) {
  auto srvgrp = in.get(TA_SRVGRP, 0, "");
  auto srvid = in.get<long>(TA_SRVID, 0, 0);
  if (srvgrp.empty() || srvid == 0) {
    throw std::logic_error("TA_SRVGRP and TA_SRVID required");
  }
  auto grp = std::find_if(t.groups.begin(), t.groups.end(), [&](auto &g) {
    return g.srvgrp == srvgrp;
  });
  if (grp == t.groups.end()) {
    throw std::logic_error("Unknown T_GROUP " + srvgrp);
  }
  auto it = std::find_if(t.servers.begin(), t.servers.end(), [&](auto &s) {
    return s.srvgrp == srvgrp && s.srvid == srvid;
  });
  bool creating = it == t.servers.end();
  if (creating) {
    if (!is_new(in)) {
      throw std::logic_error(
          "T_SERVER does not exist, TA_STATE=NEW required to create");
    }
    auto name = in.get(TA_SERVERNAME, 0, "");
    if (name.empty()) {
      throw std::logic_error("TA_SERVERNAME required");
    }
    fux::cfg::server s;
    s.srvgrp = srvgrp;
    s.srvid = srvid;
    s.servername = name;
    t.servers.push_back(std::move(s));
    it = std::prev(t.servers.end());
  } else if (is_new(in)) {
    throw std::logic_error("T_SERVER already exists");
  }

  auto inv = in.get(TA_STATE, 0, "");
  if (!creating && inv == "INV") {
    t.servers.erase(it);
    return;
  }

  it->min = in.get<long>(TA_MIN, 0, it->min);
  it->max = in.get<long>(TA_MAX, 0, it->max);
  it->mindispatchthreads =
      in.get<long>(TA_MINDISPATCHTHREADS, 0, it->mindispatchthreads);
  it->maxdispatchthreads =
      in.get<long>(TA_MAXDISPATCHTHREADS, 0, it->maxdispatchthreads);
  it->clopt = in.get<std::string>(TA_CLOPT, 0, it->clopt);
  it->envfile = in.get<std::string>(TA_ENVFILE, 0, it->envfile);
  it->dependson = in.get<std::string>(TA_DEPENDSON, 0, it->dependson);
  it->rcmd = in.get<std::string>(TA_RCMD, 0, it->rcmd);
  it->servername =
      in.get<std::string>(TA_SERVERNAME, 0, it->servername);
  auto rqaddr = in.get(TA_RQADDR, 0, "");
  if (!rqaddr.empty()) {
    it->rqaddr = rqaddr;
  } else if (it->rqaddr.empty()) {
    it->rqaddr = std::to_string(grp->grpno) + "." + std::to_string(srvid);
  }
  it->grace = in.get<long>(TA_GRACE, 0, it->grace);
  it->maxgen = in.get<long>(TA_MAXGEN, 0, it->maxgen);
  it->restart = in.get<std::string>(TA_RESTART, 0, it->restart ? "Y" : "N") == "Y";
  it->replyq = in.get<std::string>(TA_REPLYQ, 0, it->replyq ? "Y" : "N") != "N";
  it->secondaryrq =
      in.get<std::string>(TA_SECONDARYRQ, 0, it->secondaryrq ? "Y" : "N") == "Y";
}

static void t_service_get(fml32buf &in, fml32buf &out) {
  auto &mibcon = getmib();
  auto services = mibcon.services();
  FLDOCC32 oc = 0;
  for (size_t i = 0; i < services.length(); i++) {
    auto &service = services.at(i);
    if (service.state == state_t::INValid ||
        !mibcon.has_active_advertisement(i)) {
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
    auto grpidx = m.find_group(srv.at(adv.server).grpno);
    out.put(TA_SRVGRP, oc,
            grpidx == mib::badoff ? "" : m.groups().at(grpidx).srvgrp);
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

static void t_server_get(fml32buf &in, fml32buf &out) {
  auto &m = getmib();
  auto servers = m.servers();
  auto groups = m.groups();

  auto grpfilter = in.get<std::string>(TA_SRVGRP, 0, "");
  auto srvid = in.get<long>(TA_SRVID, 0, -1);

  FLDOCC32 oc = 0;
  for (size_t i = 0; i < servers.length(); i++) {
    auto &s = servers.at(i);
    auto g = m.find_group(s.grpno);
    const char *grpname = g == mib::badoff ? "" : groups.at(g).srvgrp;
    if (!grpfilter.empty() && grpfilter != grpname) {
      continue;
    }
    if (srvid != -1 && srvid != s.srvid) {
      continue;
    }
    out.put(TA_SRVGRP, oc, grpname);
    out.put(TA_SRVID, oc, (long)s.srvid);
    out.put(TA_SERVERNAME, oc, s.servername);
    out.put(TA_STATE, oc,
            s.state == state_t::ACTive ? "ACTIVE" : "INACTIVE");
    out.put(TA_GENERATION, oc, (long)s.generation);
    out.put(TA_MAXGEN, oc, (long)s.maxgen);
    out.put(TA_RESTART, oc, s.restart ? "Y" : "N");
    out.put(TA_GRACE, oc, s.grace);
    out.put(TA_RQADDR, oc, m.queues().at(s.rqaddr).rqaddr);
    out.put(TA_PID, oc, (long)s.pid);
    out.put(TA_LMID, oc, m.mach().lmid);
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

}  // namespace

int tpadmcall(FBFR32 *inbuf, FBFR32 **outbuf, long flags) {
  return fux::atmi::exception_boundary(
      [&] {
        // the platform requires a preallocated FML32 output buffer and
        // rejects a NULL one before any IPC happens
        if (outbuf == nullptr || *outbuf == nullptr) {
          TPERROR(TPEINVAL, "*outbuf must point to an allocated FML32 buffer");
          return -1;
        }
        fml32buf in(&inbuf);
        fml32buf out(outbuf);

        auto klass = in.get<std::string>(TA_CLASS, 0);
        auto operation = in.get<std::string>(TA_OPERATION, 0);

        // Runtime T_SERVER view comes from the bulletin board when the
        // application is running; otherwise fall through to the file view.
        if (klass == "T_SERVER" && operation == "GET") {
          try {
            bool exists = false;
            auto tables = load(&exists);
            if (exists &&
                mib::check_node(tables.dom.ipckey) == node_state::running) {
              t_server_get(in, out);
              return 0;
            }
          } catch (const std::exception &) {
            // fall through to the file view
          }
        }

        if (klass == "T_QUEUE" || klass == "T_SVCGRP" || klass == "T_SERVICE") {
          if (operation == "GET") {
            if (klass == "T_QUEUE") {
              t_queue_get(in, out);
            } else if (klass == "T_SVCGRP") {
              t_svcgrp_get(in, out);
            } else {
              t_service_get(in, out);
            }
            return 0;
          }
          throw std::logic_error("Unsupported TA_OPERATION " + operation);
        }

        bool exists = false;
        auto tables = load(&exists);

        if (operation == "GET") {
          if (!exists) {
            throw std::runtime_error("No TUXCONFIG at " +
                                     fux::util::getenv("TUXCONFIG", ""));
          }
          if (klass == "T_DOMAIN") {
            get_domain(tables, out);
          } else if (klass == "T_MACHINE") {
            get_machine(tables, out);
          } else if (klass == "T_GROUP") {
            FLDOCC32 oc = 0;
            for (auto &g : tables.groups) {
              get_group(g, oc++, out);
            }
            out.put(TA_OCCURS, 0, (long)oc);
            out.put(TA_ERROR, 0, TAOK);
            return 0;
          } else if (klass == "T_SERVER") {
            FLDOCC32 oc = 0;
            for (auto &s : tables.servers) {
              get_server(s, oc++, out);
            }
            out.put(TA_OCCURS, 0, (long)oc);
            out.put(TA_ERROR, 0, TAOK);
            return 0;
          } else {
            throw std::logic_error("Unsupported TA_CLASS " + klass);
          }
          out.put(TA_OCCURS, 0, 1L);
          out.put(TA_ERROR, 0, TAOK);
          return 0;
        }

        if (operation == "GETNEXT") {
          if (!exists) {
            throw std::runtime_error("No TUXCONFIG at " +
                                     fux::util::getenv("TUXCONFIG", ""));
          }
          getnext(tables, klass, in, out);
          out.put(TA_ERROR, 0, TAOK);
          return 0;
        }

        if (operation == "SET") {
          // Runtime configuration is changed through tmadmin, never
          // through the MIB from a plain client: report TPEPROTO like
          // the platform does. Offline (no bulletin board) SETs keep
          // operating on the TUXCONFIG file.
          try {
            bool exists = false;
            auto tables = load(&exists);
            if (exists &&
                mib::check_node(tables.dom.ipckey) == node_state::running) {
              TPERROR(TPEPROTO, "SET not allowed while the application is running");
              return -1;
            }
          } catch (const std::exception &) {
            // no readable TUXCONFIG: fall through to the offline path,
            // which reports the real problem
          }
          if (klass == "T_DOMAIN") {
            set_domain(tables, exists, in, out);
          } else if (!exists) {
            throw std::logic_error("Create T_DOMAIN before " + klass);
          } else if (klass == "T_GROUP") {
            set_group(tables, in, out);
          } else if (klass == "T_SERVER") {
            set_server(tables, in, out);
          } else {
            throw std::logic_error("Unsupported TA_CLASS " + klass);
          }
          save(tables);
          out.put(TA_OCCURS, 0, 1L);
          out.put(TA_ERROR, 0, TAOK);
          return 0;
        }

        throw std::logic_error("Unsupported TA_OPERATION " + operation);
      },
      -1);
}
