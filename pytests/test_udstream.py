"""ud/ud32 stream protocol tests.

Pinned against Oracle 12.2.2 with a live echo service: a blank line
sends the current buffer (the reply lands in it, so the next field
lines accumulate onto the reply), a line with just "n" starts a new
buffer, and input pending at EOF is never sent.
"""

import os
import socket

from fuxtest import is_fuxedo, main, run, scratch

SERVER = """#!/usr/bin/env python3
import sys

import tuxedo as t


class Server:
    def tpsvrinit(self, args):
        t.tpadvertise("ECHO")
        return 0

    def ECHO(self, args):
        return t.tpreturn(t.TPSUCCESS, 0, args)


t.run(Server(), sys.argv)
"""

FLDS = """*base 7900
SRVCNM 8 string
TAG 9 string
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
"echosvr.py" SRVGRP=G1 SRVID=1 MIN=1 MAX=1
"""


def boot(dir):
    ubb = UBB.format(ipckey=55711 + os.getpid() % 400,
                     host=socket.gethostname(), dir=dir,
                     tuxdir=os.environ["TUXDIR"])
    (dir / "ud.flds").write_text(FLDS)
    (dir / "ubbconfig").write_text(ubb)
    (dir / "echosvr.py").write_text(SERVER)
    os.chmod(dir / "echosvr.py", 0o755)

    pre = {"TUXCONFIG": dir / "tuxconfig"}
    run(["tmshutdown", "-y"], env=pre, cwd=dir, expect=-1)
    run(["tmipcrm", "-y"], env=pre, cwd=dir, expect=-1)
    env = {"TUXCONFIG": dir / "tuxconfig",
           "FLDTBLDIR32": dir,
           "FIELDTBLS32": "ud.flds",
           "FLDTBLDIR": dir,
           "FIELDTBLS": "ud.flds"}
    run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir)
    run(["tmboot", "-y"], env=env, cwd=dir)

    os.environ["TUXCONFIG"] = str(dir / "tuxconfig")
    os.environ["FLDTBLDIR32"] = str(dir)
    os.environ["FIELDTBLS32"] = "ud.flds"
    os.environ["FLDTBLDIR"] = str(dir)
    os.environ["FIELDTBLS"] = "ud.flds"

    import time
    import tuxedo as t
    deadline = time.time() + 30
    while True:
        try:
            t.tpcall("ECHO", {"TAG": "probe"})
            break
        except t.XatmiException:
            pass
        assert time.time() < deadline, "domain did not boot in time"
        time.sleep(0.5)
    return t


def test_ud32_gendata_style():
    if not is_fuxedo():
        print("skipping ud32 stream test on non-fuxedo install")
        return
    with scratch("udstream") as dir:
        t = boot(dir)
        proc = run(["ud32"],
                   input="n\nSRVCNM\tECHO\nTAG\tone\n\n"
                         "n\nSRVCNM\tECHO\nTAG\ttwo\n\n")
        assert proc.stdout.count("TAG\tone") == 2, proc.stdout
        assert proc.stdout.count("TAG\ttwo") == 2, proc.stdout

        t.tpterm()
        run(["tmshutdown", "-y"],
            env={"TUXCONFIG": dir / "tuxconfig"}, cwd=dir)


def test_ud32_accumulates_without_n():
    if not is_fuxedo():
        print("skipping ud32 stream test on non-fuxedo install")
        return
    with scratch("udstream") as dir:
        t = boot(dir)
        # the reply survives the send: the second chunk appends onto it
        proc = run(["ud32"],
                   input="SRVCNM\tECHO\nTAG\tone\n\nTAG\ttwo\n\n")
        assert proc.stdout.count("TAG\tone") == 4, proc.stdout
        assert proc.stdout.count("TAG\ttwo") == 2, proc.stdout

        t.tpterm()
        run(["tmshutdown", "-y"],
            env={"TUXCONFIG": dir / "tuxconfig"}, cwd=dir)


def test_ud32_drops_pending_at_eof():
    if not is_fuxedo():
        print("skipping ud32 stream test on non-fuxedo install")
        return
    with scratch("udstream") as dir:
        t = boot(dir)
        # no trailing blank: the pending buffer is never sent
        proc = run(["ud32"], input="n\nSRVCNM\tECHO\nTAG\tone\n")
        assert proc.stdout.count("TAG") == 0, proc.stdout

        t.tpterm()
        run(["tmshutdown", "-y"],
            env={"TUXCONFIG": dir / "tuxconfig"}, cwd=dir)


def test_ud32_blank_resends_same_buffer():
    if not is_fuxedo():
        print("skipping ud32 stream test on non-fuxedo install")
        return
    with scratch("udstream") as dir:
        t = boot(dir)
        # every blank line sends again: sent + reply, three times
        proc = run(["ud32"],
                   input="n\nSRVCNM\tECHO\nTAG\tone\n\n\n\n")
        assert proc.stdout.count("TAG\tone") == 6, proc.stdout

        t.tpterm()
        run(["tmshutdown", "-y"],
            env={"TUXCONFIG": dir / "tuxconfig"}, cwd=dir)


def test_ud16_blank_sends_eof_drops():
    if not is_fuxedo():
        print("skipping ud16 stream test on non-fuxedo install")
        return
    with scratch("udstream") as dir:
        t = boot(dir)
        # a terminated buffer reaches the (missing) service once;
        # unterminated input at EOF is dropped
        proc = run(["ud", "-s", "NOPE"], input="TAG\tone\n\n", expect=None)
        assert proc.stderr.count("failed") == 1, proc.stderr
        proc = run(["ud", "-s", "NOPE"], input="TAG\tone\n", expect=None)
        assert proc.stderr.count("failed") == 0, proc.stderr

        t.tpterm()
        run(["tmshutdown", "-y"],
            env={"TUXCONFIG": dir / "tuxconfig"}, cwd=dir)


if __name__ == "__main__":
    main(globals())
