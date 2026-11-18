"""Workstation clients: WSL listener + WSNADDR clients.

A domain boots with a WSL server in its own group; clients run in
subprocesses with WSNADDR set and no TUXCONFIG, over TLS with
trust-on-first-use pins in ~/.fuxkeys (redirected into the scratch
directory).
"""

import os
import socket
import sys

import tuxedo as t

import test_online_updates as tou
from fuxtest import main, run, scratch

if not tou.is_fuxedo():
    print("not a fuxedo installation, skipping workstation tests")
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
WS  LMID=tuxapp GRPNO=3

*SERVERS
"onlinesvr.py" SRVGRP=APP SRVID=1 MIN=1 MAX=1
WSL SRVGRP=WS SRVID=1 CLOPT="-n //localhost:{port} -- "

*SERVICES
"""

SERVER = """#!/usr/bin/env python3
import sys

import tuxedo as t


class Server:
    def tpsvrinit(self, args):
        t.tpadvertise("PING")
        return 0

    def tpsvrdone(self):
        pass

    def PING(self, args):
        return t.tpreturn(t.TPSUCCESS, 7, b"PONG")


if __name__ == "__main__":
    t.run(Server(), sys.argv)
"""

CLIENT = """
import sys
import tuxedo as t

t.tpinit(None)
out = t.tpcall("PING", b"hi")
assert out.data == b"PONG", out.data
assert out.rcode == 7, out.rcode
# a second call exercises reply correlation on a warm link
out = t.tpcall("PING", b"again")
assert out.data == b"PONG", out.data
try:
    t.tpbegin(30, 0)
    raise AssertionError("tpbegin must be refused over workstation links")
except t.XatmiException as e:
    assert e.code == t.TPEPROTO, e.code
t.tpterm()
"""


def test_workstation_client():
    port = 27101 + os.getpid() % 500
    with scratch("wsdom") as dir:
        ubb = UBB.format(ipckey=51111 + os.getpid() % 500,
                         host=socket.gethostname(), dir=dir,
                         tuxdir=os.environ["TUXDIR"], port=port)
        (dir / "ubbconfig").write_text(ubb)
        (dir / "onlinesvr.py").write_text(SERVER)
        os.chmod(dir / "onlinesvr.py", 0o755)
        (dir / "home").mkdir()
        env = {"TUXCONFIG": dir / "tuxconfig",
               "FLDTBLDIR32": os.path.join(os.environ["TUXDIR"], "udataobj"),
               "FIELDTBLS32": "tpadm",
               "PYTHONPATH": os.pathsep.join(
                   os.path.abspath(p)
                   for p in os.environ.get("PYTHONPATH", "").split(os.pathsep)
                   if p)}
        run(["tmipcrm", "-y"], env=env, cwd=dir, expect=-1)
        run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir)
        run(["tmboot", "-y"], env=env, cwd=dir)
        try:
            import time
            os.environ["TUXCONFIG"] = str(dir / "tuxconfig")
            deadline = time.time() + 30
            while True:
                try:
                    out = t.tpadmcall({"TA_CLASS": "T_SERVICE",
                                       "TA_OPERATION": "GET"}).data
                    if "PING" in out.get("TA_SERVICENAME", []):
                        break
                except t.XatmiException:
                    pass
                assert time.time() < deadline, "PING did not advertise"
                time.sleep(0.5)
            wsenv = {"WSNADDR": f"//localhost:{port}",
                     "HOME": dir / "home"}
            run([sys.executable, "-c", CLIENT], env=wsenv, cwd=dir / "home")
            # the client pinned the server identity, the server kept its
            # own keys under the application directory
            pins = (dir / "home" / ".fuxkeys" / "pins")
            assert any(p.name.startswith("wsl_localhost")
                       for p in pins.iterdir()), list(pins.iterdir())
            assert (dir / ".fuxkeys" /
                    f"identity-wsl_{port}.pem").exists()
        finally:
            run(["tmshutdown", "-y"], env=env, cwd=dir, expect=None)


if __name__ == "__main__":
    main(globals())
