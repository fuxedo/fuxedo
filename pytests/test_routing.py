"""Data-dependent routing (*ROUTING) tests.

Vendor neutral: the same assertions were pinned against Oracle 12.2
(runtime behavior in private/notes/routing-pins.md). Two groups offer
the same services; the request buffer's routing field decides which
group serves the call. Values outside every range (and buffers missing
the field) fail with TPESYSTEM on both platforms.
"""

import os
import socket

from fuxtest import main, run, scratch

FLDS = """*base 7700
ROUTE_VALUE 1 long
ROUTE_NAME 2 string
"""


# two identical servers except for the tag they answer with
def server_source(tag):
    return f"""#!/usr/bin/env python3
import sys

import tuxedo as t


class Server:
    def tpsvrinit(self, args):
        t.tpadvertise("ROUTED")
        t.tpadvertise("ROUTEDS")
        return 0

    def ROUTED(self, args):
        return t.tpreturn(t.TPSUCCESS, 0, "{tag}")

    def ROUTEDS(self, args):
        return t.tpreturn(t.TPSUCCESS, 0, "{tag}")


t.run(Server(), sys.argv)
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
G2 LMID=tuxapp GRPNO=2

*SERVERS
"routed1.py" SRVGRP=G1 SRVID=1 MIN=1 MAX=1
"routed2.py" SRVGRP=G2 SRVID=2 MIN=1 MAX=1

*SERVICES
ROUTED ROUTING=ROUTE_NUM
ROUTEDS ROUTING=ROUTE_STR

*ROUTING
ROUTE_NUM FIELD=ROUTE_VALUE BUFTYPE="FML32" RANGES="0-10:G1,11-20:G2"
ROUTE_STR FIELD=ROUTE_NAME BUFTYPE="FML32" RANGES="'ABCD':G1,'WXYZ':G2"
"""


def boot(dir):
    ubb = UBB.format(ipckey=49111 + os.getpid() % 500,
                     host=socket.gethostname(), dir=dir,
                     tuxdir=os.environ["TUXDIR"])
    (dir / "route.flds").write_text(FLDS)
    (dir / "ubbconfig").write_text(ubb)
    (dir / "routed1.py").write_text(server_source("G1"))
    (dir / "routed2.py").write_text(server_source("G2"))
    for name in ("routed1.py", "routed2.py"):
        os.chmod(dir / name, 0o755)

    pre = {"TUXCONFIG": dir / "tuxconfig"}
    run(["tmshutdown", "-y"], env=pre, cwd=dir, expect=-1)
    run(["tmipcrm", "-y"], env=pre, cwd=dir, expect=-1)

    env = {"TUXCONFIG": dir / "tuxconfig",
           "FLDTBLDIR32": dir,
           "FIELDTBLS32": "route.flds"}
    run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir)
    run(["tmboot", "-y"], env=env, cwd=dir)

    os.environ["TUXCONFIG"] = str(dir / "tuxconfig")
    os.environ["FLDTBLDIR32"] = str(dir)
    os.environ["FIELDTBLS32"] = "route.flds"

    import time
    import tuxedo as t
    deadline = time.time() + 30
    while True:
        try:
            if t.tpcall("ROUTED", {"ROUTE_VALUE": 5}).data == "G1":
                break
        except t.XatmiException:
            pass
        assert time.time() < deadline, "domain did not boot in time"
        time.sleep(0.5)
    return t


def test_routing():
    with scratch("routing") as dir:
        t = boot(dir)

        assert t.tpcall("ROUTED", {"ROUTE_VALUE": 5}).data == "G1"
        assert t.tpcall("ROUTED", {"ROUTE_VALUE": 15}).data == "G2"

        for payload in ({"ROUTE_VALUE": 99}, {}):
            try:
                t.tpcall("ROUTED", payload)
                raise AssertionError(f"expected TPESYSTEM for {payload}")
            except t.XatmiException as e:
                assert e.code == t.TPESYSTEM, f"unexpected code {e.code}"

        assert t.tpcall("ROUTEDS", {"ROUTE_NAME": "ABCD"}).data == "G1"
        assert t.tpcall("ROUTEDS", {"ROUTE_NAME": "WXYZ"}).data == "G2"

        # a buffer type outside BUFTYPE skips routing and is dispatched
        # to any group, on both platforms
        assert t.tpcall("ROUTED", "plain string").data in ("G1", "G2")

        # detach before shutdown: Oracle refuses to shut the BBL while
        # clients are still attached
        t.tpterm()
        run(["tmshutdown", "-y"],
            env={"TUXCONFIG": dir / "tuxconfig"}, cwd=dir)

        out = run(["tmunloadcf"],
                  env={"TUXCONFIG": dir / "tuxconfig"}, cwd=dir).stdout
        assert "*ROUTING" in out
        assert 'RANGES="0-10:G1,11-20:G2"' in out


if __name__ == "__main__":
    main(globals())
