"""BBL admin-queue backpressure.

When a server's request queue is stuffed full (a flood of TPNOBLOCK
calls the slow service cannot drain), the bulletin board must stay
responsive: admin operations on other servers keep working and no BBL
housekeeping send wedges behind the full queue.

Covers the fuxedo guarantee: BBL's internal sends use a bounded
cadence-retry window instead of an unbounded blocking msgsnd, so a
full client/server queue degrades the domain instead of deadlocking
its brain.
"""

import os
import socket

import tuxedo as t

import test_online_updates as tou
from fuxtest import main, run

if not tou.is_fuxedo():
    print("not a fuxedo installation, skipping admin queue tests")
    raise SystemExit(0)

UBB = """*RESOURCES
SECURITY NONE
MASTER tuxapp
MODEL SHM
IPCKEY {ipckey}

*MACHINES
"{host}" LMID=tuxapp APPDIR="{dir}" TUXCONFIG="{dir}/tuxconfig" TUXDIR="{tuxdir}"

*GROUPS
G1 LMID=tuxapp GRPNO=1

*SERVERS
"floodsvr.py" SRVGRP=G1 SRVID=1 MIN=1 MAX=1 RESTART=N MAXGEN=1
"pingsvr.py" SRVGRP=G1 SRVID=2 MIN=1 MAX=1

*SERVICES
"""

FLOOD_SVR = """#!/usr/bin/env python3
import sys
import time

import tuxedo as t


class Server:
    def tpsvrinit(self, args):
        t.tpadvertise("FLOOD")
        return 0

    def tpsvrdone(self):
        pass

    def FLOOD(self, args):
        time.sleep(0.05)
        return t.tpreturn(t.TPSUCCESS, 0, b"done")


if __name__ == "__main__":
    t.run(Server(), sys.argv)
"""

PING_SVR = """#!/usr/bin/env python3
import sys

import tuxedo as t


class Server:
    def tpsvrinit(self, args):
        t.tpadvertise("PING")
        return 0

    def tpsvrdone(self):
        pass

    def PING(self, args):
        return t.tpreturn(t.TPSUCCESS, 0, "PONG")


if __name__ == "__main__":
    t.run(Server(), sys.argv)
"""

FLOOD_CLIENT = """
import sys
import time
import tuxedo as t

t.tpinit(None)
t.tpcall("PING", "hi")  # wait until the domain is fully up
n = 0
blocked = 0
deadline = time.time() + 120
while time.time() < deadline and not blocked:
    try:
        # 512-byte payloads fill the queue in far fewer messages, so
        # the fill does not race the sender's rate on a loaded box
        t.tpacall("FLOOD", b"x" * 512, t.TPNOBLOCK | t.TPNOREPLY)
        n += 1
    except t.XatmiException as e:
        if e.code == t.TPEBLOCK:
            blocked = 1
            break
        raise
print(f"flooded with {n} messages, blocked={blocked}", flush=True)
if n == 0:
    raise AssertionError("queue never accepted a single message")
# keep the pressure on: without this the slow FLOOD server drains the
# queue while we sleep and the queue never stays full
end = time.time() + 150
while time.time() < end:
    try:
        t.tpacall("FLOOD", b"x" * 512, t.TPNOBLOCK | t.TPNOREPLY)
    except t.XatmiException:
        time.sleep(0.05)
t.tpterm()
"""


def test_admin_queue_backpressure():
    import sys
    with tou.scratch("admq") as dir:
        ubb = UBB.format(ipckey=48711 + os.getpid() % 500,
                         host=socket.gethostname(), dir=dir,
                         tuxdir=os.environ["TUXDIR"])
        (dir / "ubbconfig").write_text(ubb)
        (dir / "floodsvr.py").write_text(FLOOD_SVR)
        (dir / "pingsvr.py").write_text(PING_SVR)
        os.chmod(dir / "floodsvr.py", 0o755)
        os.chmod(dir / "pingsvr.py", 0o755)
        env = {"TUXCONFIG": dir / "tuxconfig",
               "FLDTBLDIR32": os.path.join(os.environ["TUXDIR"], "udataobj"),
               "FIELDTBLS32": "tpadm",
               "PYTHONPATH": os.pathsep.join(
                   os.path.abspath(p)
                   for p in os.environ.get("PYTHONPATH", "").split(os.pathsep)
                   if p)}
        os.environ["TUXCONFIG"] = str(dir / "tuxconfig")
        run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir)
        run(["tmboot", "-y"], env=env, cwd=dir)
        try:
            # start the flood in a helper process; it stops when the
            # queue starts answering TPEBLOCK, then holds
            import subprocess
            flood = subprocess.Popen(
                [sys.executable, "-u", "-c", FLOOD_CLIENT],
                env={**os.environ, **{k: str(v) for k, v in env.items()}},
                cwd=dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True)
            try:
                import time
                # wait for the queue to saturate
                deadline = time.time() + 60
                while True:
                    out = subprocess.run(
                        [sys.executable, "-c",
                         "import tuxedo as t; t.tpinit(None); "
                         "t.tpacall('FLOOD', b'y' * 512, t.TPNOBLOCK | "
                         "t.TPNOREPLY); print('not full')"],
                        env={**os.environ, **
                             {k: str(v) for k, v in env.items()}},
                        cwd=dir, capture_output=True, text=True)
                    if "not full" not in out.stdout:
                        break
                    assert time.time() < deadline, "queue never filled"
                    time.sleep(0.5)

                # BBL stays responsive: service listing works and the
                # other server keeps serving while the flood holds
                proc = tou.tmadmin(env, "psc -s PING", dir)
                assert "PING" in proc.stdout
                out = subprocess.run(
                    [sys.executable, "-c",
                     "import tuxedo as t; t.tpinit(None); "
                     "print(t.tpcall('PING', 'hi').data)"],
                    env={**os.environ, **
                         {k: str(v) for k, v in env.items()}},
                    cwd=dir, capture_output=True, text=True)
                assert "PONG" in out.stdout, out.stdout + out.stderr
            finally:
                flood.terminate()
                try:
                    fout, _ = flood.communicate(timeout=10)
                except subprocess.TimeoutExpired:
                    flood.kill()
                    fout, _ = flood.communicate()
                print(f"flood client output: {fout}")
            # the flood server is taken down with the direct-signal
            # path so the queued flood messages do not delay teardown
            run(["tmshutdown", "-k", "KILL", "-s", "floodsvr.py", "-y"],
                env=env, cwd=dir)
            run(["tmshutdown", "-s", "pingsvr.py", "-y"], env=env, cwd=dir)
        finally:
            run(["tmshutdown", "-y"], env=env, cwd=dir)


if __name__ == "__main__":
    main(globals())
