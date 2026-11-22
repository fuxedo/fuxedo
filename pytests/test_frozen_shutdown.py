"""Frozen-at-shutdown restart semantics.

Pinned against Oracle 12.2.2:
- tmshutdown blocks in the shutdown reply wait until the server
  finishes tpsvrdone and exits, however long that takes (a 25s sleep
  kept it waiting 28s; a 120s sleep kept it waiting 2m03s)
- booting the same server while the old process is still alive
  reports "Duplicate server" and starts nothing; the server comes up
  normally once the old process has exited
"""

import os
import socket
import subprocess
import time

import tuxedo as t

import test_online_updates as tou
from fuxtest import main, run

if not tou.is_fuxedo():
    print("not a fuxedo installation, skipping frozen shutdown tests")
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
"frozensvr.py" SRVGRP=G1 SRVID=1 MIN=1 MAX=1

*SERVICES
"""

FREEZE_SECONDS = 8

SERVER = """#!/usr/bin/env python3
import sys
import time

import tuxedo as t


class Server:
    def tpsvrinit(self, args):
        t.tpadvertise("PING")
        return 0

    def tpsvrdone(self):
        # freeze at shutdown: the polite shutdown must wait for this
        time.sleep(%d)
        with open("%s/done", "w") as f:
            f.write("exited")

    def PING(self, args):
        return t.tpreturn(t.TPSUCCESS, 0, "PONG")


if __name__ == "__main__":
    t.run(Server(), sys.argv)
""" % (FREEZE_SECONDS, "{dir}")


def test_frozen_shutdown_restart():
    with tou.scratch("frozen") as dir:
        ubb = UBB.format(ipckey=48511 + os.getpid() % 500,
                         host=socket.gethostname(), dir=dir,
                         tuxdir=os.environ["TUXDIR"])
        (dir / "ubbconfig").write_text(ubb)
        (dir / "frozensvr.py").write_text(SERVER.format(dir=dir))
        os.chmod(dir / "frozensvr.py", 0o755)
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

        # booting while running is refused (sanity for the duplicate
        # gate the freeze test relies on)
        proc = run(["tmboot", "-s", "frozensvr.py", "-y"], env=env, cwd=dir)
        assert "Duplicate" in (proc.stdout + proc.stderr)

        started = time.time()
        run(["tmshutdown", "-y"], env=env, cwd=dir)
        waited = time.time() - started
        assert waited >= FREEZE_SECONDS, \
            "tmshutdown returned after %.1fs, before the freeze ended" % waited
        assert (dir / "done").exists(), "tpsvrdone did not finish"

        # the server boots again cleanly after the freeze is over; the
        # check runs in a fresh process because this one attached to
        # the bulletin board of the first boot
        run(["tmboot", "-s", "frozensvr.py", "-y"], env=env, cwd=dir)
        import sys
        PING = """
import sys
import time
import tuxedo as t
t.tpinit(None)
deadline = time.time() + 30
while True:
    try:
        if t.tpcall("PING", "hi").data == "PONG":
            break
    except t.XatmiException:
        pass
    if time.time() > deadline:
        raise AssertionError("server did not restart")
    time.sleep(0.5)
t.tpterm()
"""
        run([sys.executable, "-c", PING], env=env, cwd=dir)
        run(["tmshutdown", "-y"], env=env, cwd=dir)


if __name__ == "__main__":
    main(globals())
