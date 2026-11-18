"""tmloadcf(1) / tmunloadcf(1) CLI compatibility tests.

Vendor neutral. Assertions that differ between fuxedo and Oracle today are
gated with is_fuxedo() and marked as gaps to close (see tasks.md P0).
"""

import os
import socket

from fuxtest import is_fuxedo, main, run, scratch

UBB = """*RESOURCES
IPCKEY {ipckey}
MASTER tuxapp
MODEL SHM
SECURITY NONE

*MACHINES
"{host}" LMID=tuxapp APPDIR="{dir}" TUXCONFIG="{dir}/tuxconfig" TUXDIR="{tuxdir}"

*GROUPS
G1 LMID=tuxapp GRPNO=1

*SERVERS
sleeper SRVGRP=G1 SRVID=1
"""


def write_ubb(dir):
    ubb = dir / "ubbconfig"
    ubb.write_text(
        UBB.format(host=socket.gethostname(), dir=dir,
                   tuxdir=os.environ["TUXDIR"],
                   ipckey=32769 + os.getpid() % 1000))
    return ubb


def test_help():
    proc = run(["tmloadcf", "-h"], expect=-1)
    usage = proc.stdout + proc.stderr
    for flag in ("-n", "-y", "-c"):
        assert flag in usage


def test_syntax_check():
    with scratch("tmloadcf-n") as dir:
        ubb = write_ubb(dir)
        tuxconfig = dir / "tuxconfig"
        run(["tmloadcf", "-n", ubb], env={"TUXCONFIG": tuxconfig})
        assert not tuxconfig.exists()


def test_load_and_unload():
    with scratch("tmloadcf-y") as dir:
        ubb = write_ubb(dir)
        tuxconfig = dir / "tuxconfig"
        run(["tmloadcf", "-y", ubb], env={"TUXCONFIG": tuxconfig})
        assert tuxconfig.exists()
        proc = run(["tmunloadcf"], env={"TUXCONFIG": tuxconfig})
        assert "*RESOURCES" in proc.stdout
        assert str(32769 + os.getpid() % 1000) in proc.stdout
        # loading twice with -y works on both
        run(["tmloadcf", "-y", ubb], env={"TUXCONFIG": tuxconfig})


def test_ipc_resources_flag():
    with scratch("tmloadcf-c") as dir:
        ubb = write_ubb(dir)
        tuxconfig = dir / "tuxconfig"
        proc = run(["tmloadcf", "-c", ubb], env={"TUXCONFIG": tuxconfig})
        out = proc.stdout.lower()
        assert "ipc" in out or "resources" in out
        assert not tuxconfig.exists()


def test_load_from_stdin():
    with scratch("tmloadcf-stdin") as dir:
        ubb = write_ubb(dir)
        tuxconfig = dir / "tuxconfig"
        run(["tmloadcf", "-y", "-"], input=ubb.read_text(),
            env={"TUXCONFIG": tuxconfig})
        assert tuxconfig.exists()
        proc = run(["tmunloadcf"], env={"TUXCONFIG": tuxconfig})
        assert str(32769 + os.getpid() % 1000) in proc.stdout


def test_missing_tuxconfig_env():
    with scratch("tmloadcf-env") as dir:
        ubb = write_ubb(dir)
        env = {k: v for k, v in os.environ.items() if k != "TUXCONFIG"}
        proc = run(["tmloadcf", "-y", ubb], expect=-1, env=env)
        # fuxedo exits -1, Oracle exits 1
        assert proc.returncode in (1, 255)


def test_bad_config_fails():
    with scratch("tmloadcf-bad") as dir:
        ubb = dir / "ubbconfig"
        ubb.write_text("*RESOURCES\nIPCKEY not_a_number\n")
        tuxconfig = dir / "tuxconfig"
        run(["tmloadcf", "-y", ubb], expect=-1, env={"TUXCONFIG": tuxconfig})
        assert not tuxconfig.exists()


if __name__ == "__main__":
    main(globals())
