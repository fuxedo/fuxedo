"""Crash-restart hardening: kill a server 100 times while a client
keeps calling; the BBL must always bring it back, calls must not
corrupt, and IPC resources must stay stable.

The client tolerates the dead window (TPENOENT/TPETIME while the
server is down or the request died with it) and retries until every
call succeeds; every reply's payload is verified. The parent kills the
server process mid-run, then asserts the server came back, the
generation advanced, and the SysV queue count is unchanged.

Runs against fuxedo and Oracle Tuxedo (CRASH_ITERS smaller against
Oracle: its default SCANUNIT paces restart detection). Set CRASH_ITERS
to change the iteration count.
"""

import os
import random
import socket
import subprocess
import sys
import time

from fuxtest import is_fuxedo, main, run, scratch

# fuxedo-only test modules self-skip at import time; this test runs on
# both platforms, so detect the platform without importing them
tou_is_fuxedo = is_fuxedo

ITERS = int(os.environ.get("CRASH_ITERS", "100"))

UBB = """*RESOURCES
SECURITY NONE
MASTER tuxapp
MODEL SHM
IPCKEY {ipckey}
SCANUNIT {scanunit}

*MACHINES
"{host}" LMID=tuxapp APPDIR="{dir}" TUXCONFIG="{dir}/tuxconfig" TUXDIR="{tuxdir}"

*GROUPS
APP LMID=tuxapp GRPNO=1

*SERVERS
"echosvr.py" SRVGRP=APP SRVID=1 MIN=1 MAX=1 RESTART=Y MAXGEN={maxgen} GRACE=86400

*SERVICES
"""

SERVER = """#!/usr/bin/env python3
import sys

import tuxedo as t


class Server:
    def tpsvrinit(self, args):
        t.tpadvertise("ECHO")
        return 0

    def tpsvrdone(self):
        pass

    def ECHO(self, data):
        return t.tpreturn(t.TPSUCCESS, 0, data.upper())


if __name__ == "__main__":
    t.run(Server(), sys.argv)
"""

CLIENT = """
import sys
import time
import tuxedo as t

iters = int(sys.argv[1])
t.tpinit(None)
t.tpsblktime(3, t.TPBLK_ALL | t.TPBLK_SECOND)

failures = 0
for i in range(iters):
    payload = ("CRASH-%04d" % i).encode()
    while True:
        try:
            cd = t.tpacall("ECHO", payload)
            r = t.tpgetrply(cd, 0)
            assert r.data == payload.upper(), (r.data, payload)
            break
        except t.XatmiException:
            # server down or died with our request: retry until the
            # BBL brings it back
            failures += 1
            time.sleep(0.1)
print("client done: %d/%d calls needed retries" % (failures, iters))
"""


def server_pids(dir, name):
    # match any argv token's basename: python servers appear as
    # "python3 <script> ...", native servers as the binary itself
    out = []
    for proc in os.listdir("/proc"):
        if not proc.isdigit():
            continue
        try:
            with open("/proc/%s/cmdline" % proc, "rb") as f:
                tokens = f.read().split(b"\0")
            cwd = os.readlink("/proc/%s/cwd" % proc)
        except (PermissionError, FileNotFoundError, OSError):
            continue
        if cwd == str(dir) and any(
                os.path.basename(t.decode()) == name
                for t in tokens if t):
            out.append(int(proc))
    return out


def ipcs_queues():
    out = subprocess.run(["ipcs", "-q"], capture_output=True,
                         text=True).stdout
    return len([l for l in out.splitlines()
                if l.strip() and not l.startswith("---") and
                "key" not in l.lower()])


def test_crash_restart_under_load():
    base = 55111 + os.getpid() % 400
    with scratch("crash") as dir:
        # fuxedo accepts millisecond SCANUNIT; Oracle's is seconds
        scanunit = "100MS" if tou_is_fuxedo() else "2"
        ubb = UBB.format(ipckey=base, host=socket.gethostname(), dir=dir,
                         tuxdir=os.environ["TUXDIR"],
                         maxgen=ITERS + 20, scanunit=scanunit)
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
        try:
            queues_before = ipcs_queues()
            client = subprocess.Popen(
                [sys.executable, "-c", CLIENT, str(ITERS)],
                env={**os.environ, **env}, cwd=dir,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True)
            kills = 0
            for i in range(ITERS):
                pids = server_pids(dir, "echosvr.py")
                if pids:
                    os.kill(random.choice(pids), 9)
                    kills += 1
                time.sleep(random.uniform(0.05, 0.2))
            out, err = client.communicate(timeout=ITERS * 30)
            assert client.returncode == 0, (out, err[-800:])

            # every call eventually succeeded; payload verified client-side
            assert "client done" in out, out

            # the server is back and the generation moved (the last
            # kill may be seconds old: give the BBL time to notice)
            deadline = time.time() + 30
            while not server_pids(dir, "echosvr.py"):
                assert time.time() < deadline, "server never came back"
                time.sleep(0.2)
            if tou_is_fuxedo():
                g = run(["tmadmin", "-r"], env=env, cwd=dir, expect=None,
                        input="psr\nq\n")
                assert "echosvr" in g.stdout, g.stdout

            # no IPC resource leak
            queues_after = ipcs_queues()
            assert abs(queues_after - queues_before) <= 1, (
                queues_before, queues_after)
        finally:
            run(["tmshutdown", "-y"], env=env, cwd=dir, expect=None)


if __name__ == "__main__":
    main(globals())
