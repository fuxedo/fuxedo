"""AUTOTRAN enforcement tests.

Vendor neutral: pinned against Oracle 12.2 with the C probe in
tmp/scratch/autotran-probe. A service with AUTOTRAN=Y runs inside a
transaction started by the system (tpgetlev() == 1); TPFAIL aborts it
and the caller sees TPESVCFAIL; a plain service has no transaction.
A caller transaction is NOT adopted by the AUTOTRAN begin (the group
has no TMS here, so the call fails TPETRAN on both platforms).
"""

import os
import socket

from fuxtest import main, run, scratch

SERVER = """#!/usr/bin/env python3
import sys

import tuxedo as t


class Server:
    def tpsvrinit(self, args):
        t.tpadvertise("ATX")
        t.tpadvertise("ATXFAIL")
        return 0

    def ATX(self, args):
        return t.tpreturn(t.TPSUCCESS, 0, {"LEV": t.tpgetlev()})

    def ATXFAIL(self, args):
        return t.tpreturn(t.TPFAIL, 0, {"LEV": t.tpgetlev()})


t.run(Server(), sys.argv)
"""

FLDS = """*base 7800
LEV 1 long
"""

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
"atsvr.py" SRVGRP=G1 SRVID=1 MIN=1 MAX=1

*SERVICES
ATX AUTOTRAN=Y
ATXFAIL AUTOTRAN=Y
"""


def boot(dir):
    ubb = UBB.format(ipckey=55111 + os.getpid() % 400,
                     host=socket.gethostname(), dir=dir,
                     tuxdir=os.environ["TUXDIR"])
    (dir / "at.flds").write_text(FLDS)
    (dir / "ubbconfig").write_text(ubb)
    (dir / "atsvr.py").write_text(SERVER)
    os.chmod(dir / "atsvr.py", 0o755)

    pre = {"TUXCONFIG": dir / "tuxconfig"}
    run(["tmshutdown", "-y"], env=pre, cwd=dir, expect=-1)
    run(["tmipcrm", "-y"], env=pre, cwd=dir, expect=-1)
    env = {"TUXCONFIG": dir / "tuxconfig",
           "FLDTBLDIR32": dir,
           "FIELDTBLS32": "at.flds"}
    run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir)
    run(["tmboot", "-y"], env=env, cwd=dir)

    os.environ["TUXCONFIG"] = str(dir / "tuxconfig")
    os.environ["FLDTBLDIR32"] = str(dir)
    os.environ["FIELDTBLS32"] = "at.flds"

    import time
    import tuxedo as t
    deadline = time.time() + 30
    while True:
        try:
            t.tpcall("ATX", {})
            break
        except t.XatmiException:
            pass
        assert time.time() < deadline, "domain did not boot in time"
        time.sleep(0.5)
    return t


def test_autotran():
    with scratch("autotran") as dir:
        t = boot(dir)

        ok = t.tpcall("ATX", {})
        assert ok.data["LEV"] == [1], "AUTOTRAN service must see a transaction"

        # tuxedo-python deliberately does not raise on TPESVCFAIL: the
        # failed AUTOTRAN surfaces as the tperrno of the returned reply
        r = t.tpcall("ATXFAIL", {})
        assert r.rval == t.TPESVCFAIL, f"unexpected rval {r.rval}"

        # caller transaction reaching a group without a TMS fails TPETRAN
        t.tpbegin(60)
        try:
            try:
                t.tpcall("ATX", {})
                raise AssertionError("expected TPETRAN")
            except t.XatmiException as e:
                assert e.code == t.TPETRAN, f"unexpected code {e.code}"
        finally:
            t.tpabort()

        t.tpterm()
        run(["tmshutdown", "-y"],
            env={"TUXCONFIG": dir / "tuxconfig"}, cwd=dir)


if __name__ == "__main__":
    main(globals())
