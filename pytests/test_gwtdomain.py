"""Two-domain GWTDOMAIN end to end.

Two fuxedo domains boot on one machine with separate TUXCONFIG/IPCKEY and
a BDMCONFIG each. Domain one exports TOUPPER and imports TOLOWER from
domain two; domain two mirrors it. Clients run in subprocesses because a
python process attaches to one TUXCONFIG at a time.

Boot shape per domain:

    GWADM      SRVGRP=GW1 SRVID=1
    GWTDOMAIN  SRVGRP=GW1 SRVID=10
    appserver  SRVGRP=APP SRVID=1   (advertises the exported service)

Requires fuxedo; Oracle Tuxedo has no FUXBDM01 BDMCONFIG format, so these
tests skip there like the other fuxedo-only tests.
"""

import os
import socket
import subprocess
import sys
import time

import test_online_updates as tou
from fuxtest import main, run, scratch

if not tou.is_fuxedo():
    print("not a fuxedo installation, skipping GWTDOMAIN tests")
    raise SystemExit(0)

UBB = """*RESOURCES
SECURITY NONE
MASTER tuxapp
MODEL SHM
IPCKEY {ipckey}

*MACHINES
"{host}" LMID=tuxapp APPDIR="{dir}" TUXCONFIG="{dir}/tuxconfig" TUXDIR="{tuxdir}"

*GROUPS
GW1 LMID=tuxapp GRPNO=1
APP LMID=tuxapp GRPNO=2 TMSNAME=TMS TMSCOUNT=2

*SERVERS
GWADM SRVGRP=GW1 SRVID=1 CLOPT="-- "
GWTDOMAIN SRVGRP=GW1 SRVID=10 CLOPT="-- "
"appserver.py" SRVGRP=APP SRVID=1 MIN=1 MAX=1

*SERVICES
"""

DMCONFIG = """*DM_LOCAL_DOMAINS
{name} TYPE=TDOMAIN ACCESSPOINTID="{apid}" GWGRP=GW1 CONNECTION_POLICY=ON_STARTUP DMTLOGDEV="{dmtlog}" DMTLOGNAME="DMTLOG_DOM" DMTLOGSIZE=100

*DM_REMOTE_DOMAINS
{remote} TYPE=TDOMAIN ACCESSPOINTID="{rapid}"

*DM_TDOMAIN
{name} NWADDR="//localhost:{port}"
{remote} NWADDR="//localhost:{rport}" CONNECTION_POLICY=ON_STARTUP

*DM_EXPORT
{export} LACCESSPOINT={name}

*DM_IMPORT
{imp} LACCESSPOINT={name} RACCESSPOINT={remote} RNAME={imp}
"""

SERVER = """#!/usr/bin/env python3
import sys

import tuxedo as t


class Server:
    def tpsvrinit(self, args):
        t.tpadvertise("{export}")
        return 0

    def tpsvrdone(self):
        pass

    def {export}(self, args):
        return t.tpreturn(t.TPSUCCESS, 7, args.{fn}())


if __name__ == "__main__":
    t.run(Server(), sys.argv)
"""

CLIENT = """
import sys
import tuxedo as t
out = t.tpcall(sys.argv[1], sys.argv[2].encode())
assert out.data == sys.argv[3].encode(), (out.data, sys.argv[3])
assert out.rcode == 7, out.rcode
"""

TXCLIENT = """
import sys
import tuxedo as t
t.tpbegin(30, 0)
out = t.tpcall(sys.argv[1], sys.argv[2].encode())
assert out.data == sys.argv[3].encode(), (out.data, sys.argv[3])
if sys.argv[4] == "commit":
    t.tpcommit(0)
else:
    t.tpabort(0)
print("transaction", sys.argv[4], "ok")
"""


def ipcrm_key(ipckey):
    # scratch wipes the old tuxconfig before the scrub can use it, and a
    # stale bulletin board can still look alive through pid reuse; drop
    # the key's segments directly
    import re
    key = format(ipckey, "08x")
    for flag in ("-m", "-q", "-s"):
        out = subprocess.run(["ipcs", flag], capture_output=True,
                             text=True).stdout
        for line in out.splitlines():
            if line.lower().startswith("0x" + key):
                id_ = line.split()[1]
                subprocess.run(["ipcrm", flag, id_], capture_output=True)


def domain(dir, ipckey, port, name, apid, remote, rapid, rport, export,
           imp, fn):
    ubb = UBB.format(ipckey=ipckey, host=socket.gethostname(), dir=dir,
                     tuxdir=os.environ["TUXDIR"])
    dm = DMCONFIG.format(name=name, apid=apid, remote=remote, rapid=rapid,
                         port=port, rport=rport, export=export,
                         imp=imp, dmtlog=dir / "DMTLOG")
    (dir / "ubbconfig").write_text(ubb)
    (dir / "DMCONFIG").write_text(dm)
    (dir / "appserver.py").write_text(
        SERVER.format(export=export, fn=fn))
    os.chmod(dir / "appserver.py", 0o755)
    env = {"TUXCONFIG": dir / "tuxconfig",
           "BDMCONFIG": dir / "BDMCONFIG",
           "FLDTBLDIR32": os.path.join(os.environ["TUXDIR"], "udataobj"),
           "FIELDTBLS32": "tpadm",
           "PYTHONPATH": os.pathsep.join(
               os.path.abspath(p)
               for p in os.environ.get("PYTHONPATH", "").split(os.pathsep)
               if p)}
    # stale IPC poisons the next boot; scrub before, always
    run(["tmshutdown", "-y"], env=env, cwd=dir, expect=-1)
    run(["tmipcrm", "-y"], env=env, cwd=dir, expect=-1)
    # the DMTLOG device must exist before dmloadcf validates it
    run(["crdl", dir / "DMTLOG"], env=env, cwd=dir)
    # dmloadcf cross-validates GWGRP against TUXCONFIG, so the
    # application configuration lands first; a stale board from a
    # crashed previous run can outlive its tuxconfig file (scratch
    # wipes it) and look alive through pid reuse - scrub the key
    def load():
        return run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir,
                   expect=None)
    if "already running" in str(load()):
        ipcrm_key(ipckey)
        load()
    run(["dmloadcf", "-y", dir / "DMCONFIG"], env=env, cwd=dir)
    run(["tmboot", "-y"], env=env, cwd=dir)
    return env


def call(env, svc, arg, expect):
    run([sys.executable, "-c", CLIENT, svc, arg, expect],
        env=env, cwd=env["TUXCONFIG"].parent)


def wait_for_link(env1, env2):
    # ON_STARTUP dials happen at boot; give the gateways a moment, then
    # retry the first cross-domain call until both directions flow
    deadline = time.time() + 60
    while True:
        try:
            call(env1, "TOLOWER", "HeLLo", "hello")
            call(env2, "TOUPPER", "loWer", "LOWER")
            return
        except AssertionError:
            if time.time() > deadline:
                raise
            time.sleep(2)


def test_two_domains_roundtrip():
    base = 25101 + os.getpid() % 500
    with scratch("dom1") as dir1, scratch("dom2") as dir2:
        env1 = domain(dir1, 49111 + os.getpid() % 500, base,
                      "DOM1", "AA.DOM1", "DOM2", "BB.DOM2", base + 1,
                      "TOUPPER", "TOLOWER", "upper")
        env2 = domain(dir2, 49611 + os.getpid() % 500, base + 1,
                      "DOM2", "BB.DOM2", "DOM1", "AA.DOM1", base,
                      "TOLOWER", "TOUPPER", "lower")
        try:
            wait_for_link(env1, env2)
        finally:
            run(["tmshutdown", "-y"], env=env1, cwd=dir1, expect=None)
            run(["tmshutdown", "-y"], env=env2, cwd=dir2, expect=None)


def test_two_domains_transaction():
    base = 26101 + os.getpid() % 500
    with scratch("dom1") as dir1, scratch("dom2") as dir2:
        env1 = domain(dir1, 50111 + os.getpid() % 500, base,
                      "DOM1", "AA.DOM1", "DOM2", "BB.DOM2", base + 1,
                      "TOUPPER", "TOLOWER", "upper")
        env2 = domain(dir2, 50611 + os.getpid() % 500, base + 1,
                      "DOM2", "BB.DOM2", "DOM1", "AA.DOM1", base,
                      "TOLOWER", "TOUPPER", "lower")
        try:
            wait_for_link(env1, env2)
            cwd = env1["TUXCONFIG"].parent
            out = run([sys.executable, "-c", TXCLIENT, "TOLOWER", "HeLLo",
                       "hello", "commit"], env=env1, cwd=cwd)
            assert "commit ok" in out.stdout, out
            out = run([sys.executable, "-c", TXCLIENT, "TOLOWER", "HeLLo",
                       "hello", "abort"], env=env1, cwd=cwd)
            assert "abort ok" in out.stdout, out
        finally:
            run(["tmshutdown", "-y"], env=env1, cwd=dir1, expect=None)
            run(["tmshutdown", "-y"], env=env2, cwd=dir2, expect=None)


if __name__ == "__main__":
    main(globals())
