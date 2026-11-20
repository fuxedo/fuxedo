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
import pathlib
import socket
import struct
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
"{host}" LMID=tuxapp APPDIR="{dir}" TUXCONFIG="{dir}/tuxconfig" TUXDIR="{tuxdir}" {tlogdev}

*GROUPS
GW1 LMID=tuxapp GRPNO=1 TMSNAME=TMS TMSCOUNT=2
APP LMID=tuxapp GRPNO=2 TMSNAME=TMS TMSCOUNT=2
{qm_group}
*SERVERS
GWADM SRVGRP=GW1 SRVID=1 CLOPT="-- "
GWTDOMAIN SRVGRP=GW1 SRVID=10 CLOPT="-- "
"appserver.py" SRVGRP=APP SRVID=1 MIN=1 MAX=1
{qm_server}
*SERVICES
"""

QM_GROUP = 'QM1 LMID=tuxapp GRPNO=3 TMSNAME=TMSQM TMSCOUNT=2\n\tOPENINFO="TUXEDO/QM:{dir}/QS:QSPACE"'
QM_SERVER = 'TMQUEUE SRVGRP=QM1 SRVID=1 CLOPT="-s QSPACE:TMQUEUE -- "'

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
{imports}
"""

# one DM_IMPORT line per name; RNAME defaults to the service name
def import_lines(name, remote, imp):
    return "\n".join(
        f"{n} LACCESSPOINT={name} RACCESSPOINT={remote} RNAME={n}"
        for n in imp.split())

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

QCLIENT = """
import tuxedo as t

t.tpinit(None)
t.tpenqueue("QSPACE", "Q1", t.TPQCTL(), "cross-payload")
_, msg = t.tpdequeue("QSPACE", "Q1", t.TPQCTL())
assert msg == "cross-payload", msg
print("cross ok")
t.tpterm()
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

TXQCLIENT = """
import sys
import tuxedo as t
t.tpbegin(30, 0)
t.tpenqueue("QSPACE", "STRING", t.TPQCTL(), "tx-payload")
out = t.tpcall(sys.argv[1], sys.argv[2].encode())
assert out.data == sys.argv[3].encode(), (out.data, sys.argv[3])
t.tpcommit(0)
print("commit ok")
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
           imp, fn, qm=False, tlog=False):
    ubb = UBB.format(ipckey=ipckey, host=socket.gethostname(), dir=dir,
                     tuxdir=os.environ["TUXDIR"],
                     tlogdev='TLOGDEVICE="{}/TLOG"'.format(dir) if tlog else "",
                     qm_group=QM_GROUP.format(dir=dir) if qm else "",
                     qm_server=QM_SERVER if qm else "")
    dm = DMCONFIG.format(name=name, apid=apid, remote=remote, rapid=rapid,
                         port=port, rport=rport, export=export,
                         imports=import_lines(name, remote, imp),
                         dmtlog=dir / "DMTLOG")
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
    if tlog:
        run(["crdl", dir / "TLOG"], env=env, cwd=dir)
    if qm:
        run(["buildtms", "-o", "TMSQM", "-r", "TUXEDO/QM"], env=env,
            cwd=dir)
        (dir / "QS").mkdir(exist_ok=True)
        run(["qmadmin"],
            input="qspacecreate QSPACE 47695 100 6 4 9 3 errque n 16\n"
                  "qopen QSPACE\n"
                  "qcreate STRING fifo none 2 30 80% 0% -\n",
            env={**env, "QMCONFIG": dir / "QS"}, cwd=dir)
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


def test_two_domains_queue():
    """Enqueue into a queue space served by the remote domain.

    Domain two hosts QSPACE (real TMQUEUE on a /Q device); domain one
    imports QSPACE:TMQUEUE. A client in domain one enqueues and
    dequeues through the gateway; the message physically lives in
    domain two's queue directory.
    """
    base = 29101 + os.getpid() % 500
    with scratch("dom1") as dir1, scratch("dom2") as dir2:
        env1 = domain(dir1, 54111 + os.getpid() % 500, base,
                      "DOM1", "AA.DOM1", "DOM2", "BB.DOM2", base + 1,
                      "TOUPPER", "TOLOWER QSPACE:TMQUEUE", "upper")
        env2 = domain(dir2, 54611 + os.getpid() % 500, base + 1,
                      "DOM2", "BB.DOM2", "DOM1", "AA.DOM1", base,
                      "TOLOWER", "TOUPPER", "lower", qm=True)
        try:
            wait_for_link(env1, env2)
            cwd = env1["TUXCONFIG"].parent
            out = run([sys.executable, "-c", QCLIENT], env=env1, cwd=cwd)
            assert "cross ok" in out.stdout, out
            # nothing landed in domain one's storage; the roundtrip
            # itself is proven by "cross ok" (dequeue returned the
            # payload and dequeue deletes the message)
            assert not (dir1 / "QS").exists(), list((dir1 / "QS").rglob("*"))
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


def server_pids(dir, name):
    # fuxedo servers carry no appdir in their cmdline; identify them by
    # working directory (booted servers run with cwd=appdir)
    out = []
    for proc in pathlib.Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        try:
            cmd = pathlib.Path(proc / "cmdline").read_bytes().split(b"\0")[0]
            cwd = os.readlink(proc / "cwd")
        except (PermissionError, FileNotFoundError, OSError):
            continue
        if cwd == str(dir) and pathlib.Path(cmd.decode()).name == name:
            out.append(int(proc.name))
    return out


def kill_server(dir, name):
    for pid in server_pids(dir, name):
        os.kill(pid, 9)


def seed_prepared(device, name, gtrid_raw):
    # write a PREPARED record the way a crash would leave it: meta
    # pointing at page 0, one record slot [type=4][len][gtrid bytes]
    logdir = pathlib.Path(device) / name
    logdir.mkdir(parents=True, exist_ok=True)
    (logdir / ".tlog").write_bytes(struct.pack("<8sQQQ", b"FUXTLOG1", 0, 100, 0))
    page = bytearray(512)
    page[0] = 4
    page[1] = len(gtrid_raw)
    page[2:2 + len(gtrid_raw)] = gtrid_raw
    (logdir / "00000000.page").write_bytes(page)


def tlog_has(device, name, rtype, gtrid_raw):
    logdir = pathlib.Path(device) / name
    for page in sorted(logdir.glob("*.page")):
        data = page.read_bytes()
        pos = 0
        while pos + 2 <= len(data):
            t, ln = data[pos], data[pos + 1]
            if t == 0 or ln == 0 or pos + 2 + ln > len(data):
                break
            if t == rtype and data[pos + 2:pos + 2 + ln] == gtrid_raw:
                return True
            pos += 2 + ln
    return False


def last_gtrid(ulogdir):
    import re
    gtrids = []
    for ulog in pathlib.Path(ulogdir).glob("ULOG.*"):
        gtrids += re.findall(r"Calling TM in group \d+ for \d+\.([0-9a-f]+)\.",
                             ulog.read_text())
    assert gtrids, "no transaction id found in " + str(ulogdir)
    return bytes.fromhex(gtrids[-1])


def test_two_domains_crash_recovery():
    base = 27101 + os.getpid() % 500
    with scratch("dom1") as dir1, scratch("dom2") as dir2:
        env1 = domain(dir1, 52111 + os.getpid() % 500, base,
                      "DOM1", "AA.DOM1", "DOM2", "BB.DOM2", base + 1,
                      "TOUPPER", "TOLOWER", "upper", qm=True, tlog=True)
        env2 = domain(dir2, 52611 + os.getpid() % 500, base + 1,
                      "DOM2", "BB.DOM2", "DOM1", "AA.DOM1", base,
                      "TOLOWER", "TOUPPER", "lower")
        try:
            wait_for_link(env1, env2)
            out = run([sys.executable, "-c", TXQCLIENT, "TOLOWER", "HeLLo",
                       "hello"], env=env1, cwd=dir1)
            assert "commit ok" in out.stdout, out
            gtrid = last_gtrid(dir1)
            # B crashed right after its prepare vote: plant the record
            # a crash would have left, then restart B's gateway
            seed_prepared(dir2 / "DMTLOG", "DMTLOG_DOM", gtrid)
            kill_server(dir2, "GWTDOMAIN")
            time.sleep(1)
            run(["tmboot", "-s", "GWTDOMAIN", "-y"], env=env2, cwd=dir2)
            # the rebooted gateway asks A for the decision at link-up;
            # A's TLOG holds COMMIT, so B resolves the branch
            deadline = time.time() + 45
            while time.time() < deadline:
                if tlog_has(dir2 / "DMTLOG", "DMTLOG_DOM", 1, gtrid):
                    break
                time.sleep(1)
            assert tlog_has(dir2 / "DMTLOG", "DMTLOG_DOM", 1, gtrid), \
                "in-doubt branch was not resolved with COMMIT"
        finally:
            run(["tmshutdown", "-y"], env=env1, cwd=dir1, expect=None)
            run(["tmshutdown", "-y"], env=env2, cwd=dir2, expect=None)


if __name__ == "__main__":
    main(globals())
