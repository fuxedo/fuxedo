"""Careful-rollout simulation: migrate a routed service between groups
while the application keeps running.

The operator's sequence (the customer's production scenario, adapted to
what the platform supports online):

1. routing criteria list BOTH groups, old first: "MIN-MAX:OLD;NEW";
   fuxedo tries the listed groups in reverse order, so traffic lands on
   OLD while NEW is not booted
2. the new group (with its own TMS) is booted: tmboot -g NEWGRP; its
   servers advertise the service on startup
3. traffic flips to NEW by suspending OLD (tmadmin suspend -g, an
   online update); a rollback is equally a suspend of NEW
4. the old group is shut down (tmshutdown -g) - calls keep flowing

Vendor note: Oracle TM_MIB does not support adding groups to a running
bulletin board either; rollouts use pre-provisioned spare groups and
exactly this suspend/boot/shutdown dance, or a maintenance window with
a config reload. The test pins fuxedo behavior for the online path.
"""

import os
import socket

os.environ.setdefault("FLDTBLDIR32",
                      os.path.join(os.environ["TUXDIR"], "udataobj"))
os.environ.setdefault("FIELDTBLS32", "tpadm")

import tuxedo as t

from fuxtest import main, run, scratch

if not os.environ.get("TUXDIR"):
    raise SystemExit("TUXDIR is not set")

UBB = """*RESOURCES
SECURITY NONE
MASTER tuxapp
MODEL SHM
IPCKEY {ipckey}
MAXACCESSERS 30
MAXSERVERS 25
MAXSERVICES 30

*MACHINES
"{host}" LMID=tuxapp APPDIR="{dir}" TUXCONFIG="{dir}/tuxconfig" TUXDIR="{tuxdir}" TLOGDEVICE="{dir}/TLOG"

*GROUPS
OLDGRP LMID=tuxapp GRPNO=1 TMSNAME=TMS TMSCOUNT=2
NEWGRP LMID=tuxapp GRPNO=2 TMSNAME=TMS TMSCOUNT=2

*SERVERS
"oldsvr.py" SRVGRP=OLDGRP SRVID=1 MIN=1 MAX=1
"newsvr.py" SRVGRP=NEWGRP SRVID=2 MIN=1 MAX=1

*SERVICES
ROUTED ROUTING=SHIFT_PLAN

*ROUTING
SHIFT_PLAN FIELD=ROUTE_VAL BUFTYPE="FML32" RANGES="MIN-MAX:OLDGRP;NEWGRP"
"""

SERVER = """#!/usr/bin/env python3
import sys

import tuxedo as t


class Server:
    def tpsvrinit(self, args):
        t.tpadvertise("ROUTED")
        return 0

    def ROUTED(self, args):
        return t.tpreturn(t.TPSUCCESS, 0, {{"TAG": "{tag}"}})


t.run(Server(), sys.argv)
"""

FLDS = """*base 8600
ROUTE_VAL 1 long
TAG 2 string
"""


def boot(dir, group=None):
    ubb = UBB.format(ipckey=69111 + os.getpid() % 200,
                     host=socket.gethostname(), dir=dir,
                     tuxdir=os.environ["TUXDIR"])
    (dir / "ubbconfig").write_text(ubb)
    (dir / "ro.flds").write_text(FLDS)
    (dir / "oldsvr.py").write_text(SERVER.format(tag="OLD"))
    (dir / "newsvr.py").write_text(SERVER.format(tag="NEW"))
    for n in ("oldsvr.py", "newsvr.py"):
        os.chmod(dir / n, 0o755)

    pre = {"TUXCONFIG": dir / "tuxconfig"}
    run(["tmshutdown", "-y"], env=pre, cwd=dir, expect=-1)
    run(["tmipcrm", "-y"], env=pre, cwd=dir, expect=-1)
    env = {"TUXCONFIG": dir / "tuxconfig",
           "FLDTBLDIR32": str(dir),
           "FIELDTBLS32": "ro.flds"}
    run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir)
    run(["tmadmin"], input='crdl -b 200 -z %s\ncrlog -m tuxapp\nq\n' %
        (dir / "TLOG"), env=env, cwd=dir)
    cmd = ["tmboot", "-y"]
    if group:
        cmd = ["tmboot", "-g", group, "-y"]
    run(cmd, env=env, cwd=dir)
    os.environ.update({k: str(v) for k, v in env.items()})
    return env


def routed_to():
    r = t.tpcall("ROUTED", {"ROUTE_VAL": 5})
    return r.data["TAG"][0]


def tmadmin(env, script):
    return run(["tmadmin"], input=script + "\nq\n", env=env, cwd=env["TUXCONFIG"].parent)


def test_careful_rollout():
    with scratch("rollout") as dir:
        env = boot(dir, group="OLDGRP")
        try:
            assert routed_to() == "OLD", "phase 1: traffic must stay on OLD"

            # phase 2: boot the new group with its own TMS; servers
            # advertise on startup, traffic must not move yet
            run(["tmboot", "-g", "NEWGRP", "-y"], env=env, cwd=dir)
            import time
            deadline = time.time() + 30
            while routed_to() != "NEW":
                if time.time() > deadline:
                    raise AssertionError("traffic did not flip to NEW")
                time.sleep(0.5)

            # rollback drill: suspend NEW, traffic returns to OLD
            tmadmin(env, "suspend -g NEWGRP -i 2")
            assert routed_to() == "OLD", "rollback: suspend NEW must restore OLD"

            # re-flip and decommission the old group
            tmadmin(env, "resume -g NEWGRP -i 2\nsuspend -g OLDGRP -i 1")
            assert routed_to() == "NEW", "resume NEW + suspend OLD must land on NEW"
            run(["tmshutdown", "-g", "OLDGRP", "-y"], env=env, cwd=dir)
            assert routed_to() == "NEW", "calls must survive OLD going away"
        finally:
            run(["tmshutdown", "-y"], env=env, cwd=dir, expect=None)


if __name__ == "__main__":
    main(globals())
