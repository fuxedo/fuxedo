"""Transaction timeout on an in-flight call.

Pinned against Oracle 12.2.2 (probe tmp/scratch/txtimeout): a client
transaction with a 1-second timeout around a 2-second service fails the
CALL with TPETIME (the deadline interrupts the reply wait), and
tpcommit then answers TPETIME and rolls the transaction back.
"""

import os
import socket
import time

from fuxtest import main, run, scratch

SERVER = """#!/usr/bin/env python3
import sys
import time

import tuxedo as t


class Server:
    def tpsvrinit(self, args):
        t.tpadvertise("SLEEP")
        return 0

    def SLEEP(self, args):
        time.sleep(2)
        return t.tpreturn(t.TPSUCCESS, 0, {"OK": 1})


t.run(Server(), sys.argv)
"""

FLDS = """*base 7800
OK 1 long
"""

UBB = """*RESOURCES
SECURITY NONE
MASTER tuxapp
MODEL SHM
IPCKEY {ipckey}

*MACHINES
"{host}" LMID=tuxapp APPDIR="{dir}" TLOGDEVICE="{dir}/tlog" TLOGNAME=tlog TUXCONFIG="{dir}/tuxconfig" TUXDIR="{tuxdir}"

*GROUPS
G1 LMID=tuxapp GRPNO=1 TMSNAME=TMSNULL

*SERVERS
"sleepsvr.py" SRVGRP=G1 SRVID=1 MIN=1 MAX=1
TMSNULL SRVGRP=G1 SRVID=2
"""


def boot(dir):
    ubb = UBB.format(ipckey=55311 + os.getpid() % 400,
                     host=socket.gethostname(), dir=dir,
                     tuxdir=os.environ["TUXDIR"])
    (dir / "sleep.flds").write_text(FLDS)
    (dir / "ubbconfig").write_text(ubb)
    (dir / "sleepsvr.py").write_text(SERVER)
    os.chmod(dir / "sleepsvr.py", 0o755)
    (dir / "tlog").mkdir(exist_ok=True)
    run(["buildtms", "-o", dir / "TMSNULL", "-r", "NONE"], cwd=dir)

    pre = {"TUXCONFIG": dir / "tuxconfig"}
    run(["tmshutdown", "-y"], env=pre, cwd=dir, expect=-1)
    run(["tmipcrm", "-y"], env=pre, cwd=dir, expect=-1)
    env = {"TUXCONFIG": dir / "tuxconfig",
           "FLDTBLDIR32": dir,
           "FIELDTBLS32": "sleep.flds"}
    run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir)
    run(["tmboot", "-y"], env=env, cwd=dir)

    os.environ["TUXCONFIG"] = str(dir / "tuxconfig")
    os.environ["FLDTBLDIR32"] = str(dir)
    os.environ["FIELDTBLS32"] = "sleep.flds"

    import tuxedo as t
    deadline = time.time() + 30
    while True:
        try:
            t.tpcall("SLEEP", {})
            break
        except t.XatmiException:
            pass
        assert time.time() < deadline, "domain did not boot in time"
        time.sleep(0.5)
    return t


def test_tx_timeout_fails_inflight_call():
    with scratch("txtimeout") as dir:
        t = boot(dir)

        # control: no transaction, the 2s service answers normally
        started = time.time()
        r = t.tpcall("SLEEP", {})
        assert r.data["OK"] == [1]
        assert time.time() - started >= 1.5

        # a 1s transaction around the 2s service: the call itself
        # fails with TPETIME when the deadline passes mid-wait
        t.tpbegin(1, 0)
        started = time.time()
        try:
            t.tpcall("SLEEP", {})
            raise AssertionError("call must fail TPETIME at the tx deadline")
        except t.XatmiException as e:
            assert e.code == t.TPETIME, f"expected TPETIME, got {e.code}"
        elapsed = time.time() - started
        assert elapsed < 6, f"deadline did not interrupt the wait ({elapsed}s)"

        # the timed-out transaction cannot commit
        try:
            t.tpcommit()
            raise AssertionError("commit of an expired tx must fail")
        except t.XatmiException as e:
            assert e.code == t.TPETIME, f"expected TPETIME, got {e.code}"


main(globals())
