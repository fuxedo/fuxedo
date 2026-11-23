"""Machine ENVFILE applies to joining clients, not only booted servers.

Pinned on Oracle 12.2.2 (bankapp's ud inherits FLDTBLDIR from the ubb
ENVFILE at tpinit). A client whose process environment carries no field
table variables must still resolve fields when the machine ENVFILE
provides them.
"""

import os
import socket
import subprocess

from fuxtest import check_env, is_fuxedo, main, run, scratch

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

FLDS = """*base 7950
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
    ENVFILE="{dir}/envfile"

*GROUPS
G1 LMID=tuxapp GRPNO=1

*SERVERS
"echosvr.py" SRVGRP=G1 SRVID=1 MIN=1 MAX=1
"""

ENVFILE = """# server and client environment: field tables live only here
FLDTBLDIR={dir}
FIELDTBLS=env.flds
"""


def test_client_envfile():
    if not is_fuxedo():
        print("skipping ENVFILE test on non-fuxedo install")
        return
    tuxdir = check_env()
    with scratch("envfile") as dir:
        ubb = UBB.format(ipckey=55911 + os.getpid() % 400,
                         host=socket.gethostname(), dir=dir,
                         tuxdir=tuxdir)
        (dir / "env.flds").write_text(FLDS)
        (dir / "ubbconfig").write_text(ubb)
        (dir / "envfile").write_text(ENVFILE.format(dir=dir))
        (dir / "echosvr.py").write_text(SERVER)
        os.chmod(dir / "echosvr.py", 0o755)

        env = {"TUXCONFIG": dir / "tuxconfig",
               "FLDTBLDIR32": dir,
               "FIELDTBLS32": "env.flds"}
        run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir)
        run(["tmboot", "-y"], env=env, cwd=dir)

        import time
        import tuxedo as t
        os.environ["TUXCONFIG"] = str(dir / "tuxconfig")
        os.environ["FLDTBLDIR32"] = str(dir)
        os.environ["FIELDTBLS32"] = "env.flds"
        deadline = time.time() + 30
        while True:
            try:
                t.tpcall("ECHO", {"TAG": "probe"})
                break
            except t.XatmiException:
                pass
            assert time.time() < deadline, "domain did not boot in time"
            time.sleep(0.5)
        t.tpterm()

        # the ud32 process gets NO field table variables of its own; the
        # only way SRVCNM resolves is the machine ENVFILE at tpinit
        proc_env = {
            "PATH": f"{tuxdir}/bin:/usr/bin:/bin",
            "LD_LIBRARY_PATH": f"{tuxdir}/lib",
            "TUXDIR": str(tuxdir),
            "TUXCONFIG": str(dir / "tuxconfig"),
            "HOME": os.environ.get("HOME", "/tmp"),
            "TMPDIR": os.environ["TMPDIR"],
        }
        proc = subprocess.run(
            ["ud32"], input="SRVCNM\tECHO\nTAG\tfromenv\n\n",
            capture_output=True, text=True, cwd=dir, env=proc_env)
        assert "No service name found" not in proc.stderr, proc.stderr
        assert proc.stdout.count("TAG\tfromenv") == 2, proc.stdout

        run(["tmshutdown", "-y"],
            env={"TUXCONFIG": dir / "tuxconfig"}, cwd=dir)


if __name__ == "__main__":
    main(globals())
