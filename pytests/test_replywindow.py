"""Late-reply hardening and a scatter-gather window.

Late reply: a client times out waiting for a slow service, gets TPETIME,
and the service's reply lands afterwards. The late reply must be
discarded (the platform sweeps the reply queue every few calls and logs
a warning), never delivered to an unrelated call.

Scatter-gather: 100 requests through a 4-wide tpacall window with
TPGETANY, refilling the window on every reply.

Requires fuxedo; skips on Oracle like the other fuxedo-only tests.
"""

import os
import socket
import sys
import time

import test_online_updates as tou
from fuxtest import main, run, scratch

if not tou.is_fuxedo():
    print("not a fuxedo installation, skipping reply-window tests")
    raise SystemExit(0)

UBB = """*RESOURCES
SECURITY NONE
MASTER tuxapp
MODEL SHM
IPCKEY {ipckey}

*MACHINES
"{host}" LMID=tuxapp APPDIR="{dir}" TUXCONFIG="{dir}/tuxconfig" TUXDIR="{tuxdir}"

*GROUPS
APP LMID=tuxapp GRPNO=1

*SERVERS
"echosvr.py" SRVGRP=APP SRVID=1 MIN=1 MAX=1 MINDISPATCHTHREADS=4 MAXDISPATCHTHREADS=4

*SERVICES
"""

SERVER = """#!/usr/bin/env python3
import sys
import time

import tuxedo as t


class Server:
    def tpsvrinit(self, args):
        t.tpadvertise("ECHO")
        return 0

    def tpsvrdone(self):
        pass

    def ECHO(self, data):
        if data.startswith(b"SLOW"):
            time.sleep(5)
        return t.tpreturn(t.TPSUCCESS, 0, data.upper())


if __name__ == "__main__":
    t.run(Server(), sys.argv)
"""

LATE_CLIENT = """
import sys
import time
import tuxedo as t

t.tpinit(None)
t.tpsblktime(2, t.TPBLK_ALL | t.TPBLK_SECOND)

cd1 = t.tpacall("ECHO", b"SLOW-late")
try:
    t.tpgetrply(cd1, 0)
    raise AssertionError("expected TPETIME")
except t.XatmiException as e:
    assert e.code == t.TPETIME, e.code

# a timed-out call stays outstanding: re-wait times out again, and the
# late reply would still be deliverable to a wait on this descriptor
# (install-tests/blocktime pins the platform behavior)
try:
    t.tpgetrply(cd1, 0)
    raise AssertionError("expected TPETIME on re-wait")
except t.XatmiException as e:
    assert e.code == t.TPETIME, e.code

# abandoning the call is tpcancel's job: only then is the descriptor
# protected from reuse and the late reply discardable
t.tpcancel(cd1)

# a fresh call must get its own reply, never the late one
cd2 = t.tpacall("ECHO", b"UNIQ-A1")
r = t.tpgetrply(cd2, 0)
assert r.data == b"UNIQ-A1", r.data

# the slow service replies around t=5s; wait it out, then make enough
# calls for the periodic stale-reply sweep to discard it
time.sleep(4)
for i in range(6):
    c = t.tpacall("ECHO", ("PING%d" % i).encode())
    t.tpgetrply(c, 0)

# only the reply we asked for may come back on a GETANY wait
cd3 = t.tpacall("ECHO", b"UNIQ-C3")
r = t.tpgetrply(0, t.TPGETANY)
assert r.data == b"UNIQ-C3", r.data
print("late reply discarded cleanly")
"""

SG_CLIENT = """
import sys
import tuxedo as t

t.tpinit(None)
WINDOW = 4
TOTAL = 100

inflight = {}
sent = 0
for i in range(WINDOW):
    payload = ("REQ-%03d" % i).encode()
    cd = t.tpacall("ECHO", payload)
    inflight[cd] = payload
    sent += 1

got = {}
while len(got) < TOTAL:
    r = t.tpgetrply(0, t.TPGETANY)
    expect = inflight.pop(r.cd, None)
    assert expect is not None, ("unexpected cd %d" % r.cd)
    assert r.data == expect.upper(), (r.data, expect)
    got[r.data] = True
    if sent < TOTAL:
        payload = ("REQ-%03d" % sent).encode()
        cd = t.tpacall("ECHO", payload)
        inflight[cd] = payload
        sent += 1
assert len(got) == TOTAL, len(got)
print("scatter-gather done")
"""


def boot(dir, ipckey):
    ubb = UBB.format(ipckey=ipckey, host=socket.gethostname(), dir=dir,
                     tuxdir=os.environ["TUXDIR"])
    (dir / "ubbconfig").write_text(ubb)
    (dir / "echosvr.py").write_text(SERVER)
    os.chmod(dir / "echosvr.py", 0o755)
    env = {"TUXCONFIG": dir / "tuxconfig",
           "FLDTBLDIR32": os.path.join(os.environ["TUXDIR"], "udataobj"),
           "FIELDTBLS32": "tpadm",
           "PYTHONPATH": os.pathsep.join(
               os.path.abspath(p)
               for p in os.environ.get("PYTHONPATH", "").split(os.pathsep)
               if p)}
    run(["tmshutdown", "-y"], env=env, cwd=dir, expect=-1)
    run(["tmipcrm", "-y"], env=env, cwd=dir, expect=-1)
    run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir)
    run(["tmboot", "-y"], env=env, cwd=dir)
    return env


def test_late_reply_discarded():
    with scratch("laterep") as dir:
        env = boot(dir, 54111 + os.getpid() % 500)
        try:
            out = run([sys.executable, "-c", LATE_CLIENT], env=env, cwd=dir)
            assert "discarded cleanly" in out.stdout, out
            # the sweep logs a warning when the straggler is dropped
            ulog = b" ".join(p.read_bytes() for p in dir.glob("ULOG.*"))
            assert b"discarding late reply" in ulog, ulog[-500:]
        finally:
            run(["tmshutdown", "-y"], env=env, cwd=dir, expect=None)


def test_scatter_gather_window():
    with scratch("scatter") as dir:
        env = boot(dir, 54611 + os.getpid() % 500)
        try:
            out = run([sys.executable, "-c", SG_CLIENT], env=env, cwd=dir)
            assert "scatter-gather done" in out.stdout, out
        finally:
            run(["tmshutdown", "-y"], env=env, cwd=dir, expect=None)


if __name__ == "__main__":
    main(globals())
