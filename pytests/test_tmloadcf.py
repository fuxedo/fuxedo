"""tmloadcf(1) / tmunloadcf(1) CLI compatibility tests.

Vendor neutral. Assertions that differ between fuxedo and Oracle today are
gated with is_fuxedo() and marked as gaps to close (see tasks.md P0).
"""

import os
import socket

from fuxtest import is_fuxedo, main, run, scratch

UBB = """*RESOURCES
IPCKEY 32769
MASTER tuxapp
MODEL SHM

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
                   tuxdir=os.environ["TUXDIR"]))
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
        if is_fuxedo():
            # gap: -n still writes the file (tasks.md P0.3)
            assert tuxconfig.exists()
        else:
            assert not tuxconfig.exists()


def test_load_and_unload():
    with scratch("tmloadcf-y") as dir:
        ubb = write_ubb(dir)
        tuxconfig = dir / "tuxconfig"
        run(["tmloadcf", "-y", ubb], env={"TUXCONFIG": tuxconfig})
        assert tuxconfig.exists()
        proc = run(["tmunloadcf"], env={"TUXCONFIG": tuxconfig})
        assert "*RESOURCES" in proc.stdout
        assert "32769" in proc.stdout
        # loading twice with -y works on both
        run(["tmloadcf", "-y", ubb], env={"TUXCONFIG": tuxconfig})


def test_ipc_resources_flag():
    with scratch("tmloadcf-c") as dir:
        ubb = write_ubb(dir)
        tuxconfig = dir / "tuxconfig"
        proc = run(["tmloadcf", "-c", ubb], env={"TUXCONFIG": tuxconfig})
        if not is_fuxedo():
            assert "IPC" in proc.stdout or "resources" in proc.stdout.lower()
            assert not tuxconfig.exists()
        else:
            # gap: -c is accepted but the load still happens (tasks.md P0.3)
            assert tuxconfig.exists()


def test_missing_tuxconfig_env():
    with scratch("tmloadcf-env") as dir:
        ubb = write_ubb(dir)
        env = {k: v for k, v in os.environ.items() if k != "TUXCONFIG"}
        proc = run(["tmloadcf", "-y", ubb], expect=-1, env=env)
        # subprocess maps exit(-1) to 255
        assert proc.returncode == 255


def test_bad_config_fails():
    with scratch("tmloadcf-bad") as dir:
        ubb = dir / "ubbconfig"
        ubb.write_text("*RESOURCES\nIPCKEY not_a_number\n")
        tuxconfig = dir / "tuxconfig"
        run(["tmloadcf", "-y", ubb], expect=-1, env={"TUXCONFIG": tuxconfig})
        assert not tuxconfig.exists()


if __name__ == "__main__":
    main(globals())
