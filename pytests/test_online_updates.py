"""Runtime admin updates against a running domain.

Covers the pinned Oracle facts:
tmadmin adv/unadv/suspend/resume are direct bulletin-board writes,
services become invisible to clients (TPENOENT) while T_SERVER TA_STATE
stays ACTIVE, suspend/resume flips visibility for the whole server, all
changes are temporary (lost at reboot), MIB SET from a client is
TPEPROTO while the application runs, every tmadmin command is
audit-logged, -r attaches read-only, and crdl/crlog work as interactive
commands (the customer installer drives them through tmadmin stdin).
"""

import os
import socket

os.environ.setdefault("FLDTBLDIR32",
                      os.path.join(os.environ["TUXDIR"], "udataobj"))
os.environ.setdefault("FIELDTBLS32", "tpadm")

import tuxedo as t

from fuxtest import is_fuxedo, main, run, scratch

if not is_fuxedo():
    print("not a fuxedo installation, skipping online update tests")
    raise SystemExit(0)

UBB = """*RESOURCES
SECURITY NONE
MASTER tuxapp
MODEL SHM
IPCKEY {ipckey}

*MACHINES
"{host}" LMID=tuxapp APPDIR="{dir}" TUXCONFIG="{dir}/tuxconfig" TUXDIR="{tuxdir}" TLOGDEVICE="{dir}/TLOG"

*GROUPS
G1 LMID=tuxapp GRPNO=1

*SERVERS
"onlinesvr.py" SRVGRP=G1 SRVID=1 MIN=1 MAX=1 REPLYQ=Y
"""

SERVER = """#!/usr/bin/env python3
import sys

import tuxedo as t


class Server:
    def tpsvrinit(self, args):
        t.tpadvertise("ECHO")
        t.tpadvertise("PING")
        return 0

    def tpsvrdone(self):
        pass

    def ECHO(self, args):
        return t.tpreturn(t.TPSUCCESS, 0, args)

    def PING(self, args):
        return t.tpreturn(t.TPSUCCESS, 0, "PONG")


if __name__ == "__main__":
    t.run(Server(), sys.argv)
"""


def tmadmin(env, script, dir):
    return run(["tmadmin"], input=script + "\nquit\n", env=env, cwd=dir)


def call(svc):
    try:
        return t.tpcall(svc, "hi").data
    except t.XatmiException as e:
        return e.code


def server_state(env):
    out = t.tpadmcall({
        "TA_CLASS": "T_SERVER",
        "TA_OPERATION": "GET",
        "TA_SRVGRP": "G1",
        "TA_SRVID": 1,
    }).data
    return out["TA_STATE"][0]


def advertised(svc):
    out = t.tpadmcall({
        "TA_CLASS": "T_SERVICE",
        "TA_OPERATION": "GET",
    }).data
    return svc in out.get("TA_SERVICENAME", [])


def boot(dir):
    ubb = UBB.format(ipckey=47711 + os.getpid() % 500,
                     host=socket.gethostname(), dir=dir,
                     tuxdir=os.environ["TUXDIR"])
    # stale IPC from a crashed run poisons the next boot (semaphore ids
    # get reused with the wrong shape); scrub first, always
    pre = {"TUXCONFIG": dir / "tuxconfig"}
    run(["tmshutdown", "-y"], env=pre, cwd=dir, expect=-1)
    run(["tmipcrm", "-y"], env=pre, cwd=dir, expect=-1)
    (dir / "TLOG").mkdir()
    (dir / "ubbconfig").write_text(ubb)
    (dir / "onlinesvr.py").write_text(SERVER)
    os.chmod(dir / "onlinesvr.py", 0o755)
    env = {"TUXCONFIG": dir / "tuxconfig",
           "FLDTBLDIR32": os.path.join(os.environ["TUXDIR"], "udataobj"),
           "FIELDTBLS32": "tpadm",
           # absolute paths: servers run with cwd=appdir and python
           # resolves a relative PYTHONPATH against their directory
           "PYTHONPATH": os.pathsep.join(
               os.path.abspath(p)
               for p in os.environ.get("PYTHONPATH", "").split(os.pathsep)
               if p)}
    # this process makes calls too; it needs the same environment
    os.environ["TUXCONFIG"] = str(dir / "tuxconfig")
    run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir)
    run(["tmboot", "-y"], env=env, cwd=dir)
    # tmboot returns before the servers advertise; poll like a client
    import time
    deadline = time.time() + 30
    while call("PING") != "PONG":
        assert time.time() < deadline, "server did not advertise in time"
        time.sleep(0.5)
    return env


def test_online_updates():
    # one domain per process on purpose: this process caches its bulletin
    # board attachment, so all live checks run against the first boot and
    # the reboot check runs in a fresh subprocess
    with scratch("online") as dir:
        env = boot(dir)
        try:
            # --- unadv/adv visibility ---
            assert call("ECHO") == "hi"
            assert call("PING") == "PONG"
            tmadmin(env, "unadv -g G1 -i 1 ECHO", dir)
            assert call("ECHO") == t.TPENOENT
            assert call("PING") == "PONG"
            assert not advertised("ECHO")
            tmadmin(env, "adv -g G1 -i 1 ECHO", dir)
            assert call("ECHO") == "hi"
            assert advertised("ECHO")

            # --- suspend/resume: services hidden, server stays ACTIVE ---
            tmadmin(env, "suspend -g G1 -i 1", dir)
            assert call("ECHO") == t.TPENOENT
            assert call("PING") == t.TPENOENT
            assert server_state(env) == "ACTIVE"
            tmadmin(env, "resume -g G1 -i 1", dir)
            assert call("ECHO") == "hi"
            assert call("PING") == "PONG"
            assert server_state(env) == "ACTIVE"

            # --- unknown targets fail ---
            proc = tmadmin(env, "suspend -g G1 -i 99", dir)
            assert "no such server" in proc.stderr
            proc = tmadmin(env, "adv -g G1 -i 1 NOSUCHSVC", dir)
            assert "NOSUCHSVC" in proc.stderr

            # --- MIB SET from a client is TPEPROTO while running ---
            try:
                t.tpadmcall({
                    "TA_CLASS": "T_SERVER",
                    "TA_OPERATION": "SET",
                    "TA_SRVGRP": "G1",
                    "TA_SRVID": 1,
                    "TA_MAXGEN": 9,
                })
                raise AssertionError("runtime MIB SET must fail")
            except t.XatmiException as e:
                assert e.code == t.TPEPROTO, e.code

            # --- audit trail + read-only mode ---
            tmadmin(env, "unadv -g G1 -i 1 ECHO", dir)
            assert call("ECHO") == t.TPENOENT
            ulog = " ".join(p.read_text() for p in dir.glob("ULOG.*"))
            assert "Command: unadv -g G1 -i 1 ECHO" in ulog
            proc = run(["tmadmin", "-r"], input="psr\n"
                       "resume -g G1 -i 1\nquit\n",
                       env=env, cwd=dir)
            assert "cannot become administrator" in proc.stdout
            assert "Prog Name" in proc.stdout  # psr still works
            assert "not allowed in read-only mode" in proc.stderr
            assert call("ECHO") == t.TPENOENT  # resume was refused

            # --- crdl/crlog as interactive commands ---
            proc = tmadmin(env, "crdl -b 100 -z %s" % (dir / "TLOG"), dir)
            assert (dir / "TLOG" / ".tlog").exists(), \
                "crdl failed: %s %s" % (proc.stdout, proc.stderr)
            proc = tmadmin(env, "crlog -m tuxapp", dir)
            assert "TLOG" in proc.stdout
            proc = tmadmin(env, "crlog -m NOPE", dir)
            assert "not found" in proc.stderr

            # --- runtime changes are temporary: gone after a reboot ---
            tmadmin(env, "unadv -g G1 -i 1 ECHO", dir)
            assert call("ECHO") == t.TPENOENT
        finally:
            run(["tmshutdown", "-y"], env=env, cwd=dir)

        # a reboot re-derives everything from the untouched TUXCONFIG; the
        # check runs in a fresh process because this one keeps its bulletin
        # board attachment from the first boot
        env = {"TUXCONFIG": dir / "tuxconfig"}
        run(["tmboot", "-y"], env=env, cwd=dir)
        try:
            run(["python3", "-c",
                 "import os,sys; os.environ['TUXCONFIG']=sys.argv[1];"
                 "import tuxedo as t; assert t.tpcall('ECHO', 'hi').data == 'hi'",
                 str(dir / "tuxconfig")],
                env=env, cwd=dir)
        finally:
            run(["tmshutdown", "-y"], env=env, cwd=dir)


if __name__ == "__main__":
    main(globals())
