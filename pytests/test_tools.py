"""CLI compatibility tests for the smaller fuxedo/Tuxedo tools.

Vendor neutral where possible; Oracle's ud32 needs a live domain just
like fuxedo's, so no ud32 gating is required anymore.
"""

import os
import socket
import shutil

from fuxtest import is_fuxedo, main, run, scratch


def test_mkfldhdr32_generates_header():
    tuxdir = os.environ["TUXDIR"]
    with scratch("mkfldhdr32") as dir:
        shutil.copy(os.path.join(tuxdir, "udataobj", "tpadm"), dir / "tpadm")
        run(["mkfldhdr32", "-d", dir, dir / "tpadm"])
        header = (dir / "tpadm.h").read_text()
        spaced = " ".join(header.split("\t"))
        assert "#define TA_CLASS" in spaced
        # field id values are aligned between fuxedo and Oracle
        assert "167778162" in header
        usys = os.path.join(tuxdir, "udataobj", "Usysfl32")
        if os.path.exists(usys):
            # 22.1 moved SRVCNM from tpadm to Usysfl32
            shutil.copy(usys, dir / "Usysfl32")
            run(["mkfldhdr32", "-d", dir, dir / "Usysfl32"])
            header = (dir / "Usysfl32.h").read_text()
            spaced = " ".join(header.split("\t"))
        assert "#define SRVCNM" in spaced


def test_mkfldhdr32_missing_table_fails():
    with scratch("mkfldhdr32-missing") as dir:
        run(["mkfldhdr32", "-d", dir, dir / "nope"], expect=-1)


def test_mkfldhdr32_duplicate_field_ids_ok():
    # Oracle 12.2.2 mkfldhdr32 accepts duplicate field numbers silently
    # (verified live 2026-11-18); fuxedo matches the tolerance
    with scratch("mkfldhdr32-dup") as dir:
        table = dir / "dup"
        table.write_text("*base 10000\nAAA 1 string\nBBB 1 long\n")
        run(["mkfldhdr32", "-d", dir, table])
        header = (dir / "dup.h").read_text()
        assert "#define AAA" in header
        assert "#define BBB" in header


def test_ud32_help():
    proc = run(["ud32", "-h"], expect=None)
    assert "ud32" in proc.stdout + proc.stderr


def test_ud32_requires_domain():
    # the platform joins at startup (tpinit before reading stdin); a
    # missing domain is a startup failure, not a silent passthrough
    env = {k: v for k, v in os.environ.items() if k != "TUXCONFIG"}
    run(["ud32"], input="\n", expect=-1, env=env)


def test_tmipcrm_requires_domain():
    env = {k: v for k, v in os.environ.items() if k != "TUXCONFIG"}
    run(["tmipcrm", "-y"], expect=-1, env=env)


def test_buildserver_help():
    proc = run(["buildserver", "-h"], expect=None)
    assert "buildserver" in proc.stdout + proc.stderr


def test_buildclient_help():
    proc = run(["buildclient", "-h"], expect=None)
    assert "buildclient" in proc.stdout + proc.stderr


def test_tmipcrm_accepts_positional_tuxconfig_and_n():
    if not is_fuxedo():
        # 22.1 tmipcrm refuses to run without a booted bulletin board
        print("skipping tmipcrm positional test on non-fuxedo install")
        return
    with scratch("tmipcrm-pos") as dir:
        ubb = dir / "ubbconfig"
        ubb.write_text("""*RESOURCES
IPCKEY {ipckey}
MASTER tuxapp
MODEL SHM
SECURITY NONE

*MACHINES
"{host}" LMID=tuxapp APPDIR="{dir}" TUXCONFIG="{dir}/tuxconfig" TUXDIR="{tuxdir}"
""".format(dir=dir, tuxdir=os.environ["TUXDIR"], host=socket.gethostname(),
           ipckey=33769 + os.getpid() % 1000))
        tuxconfig = dir / "tuxconfig"
        run(["tmloadcf", "-y", ubb], env={"TUXCONFIG": tuxconfig})
        # positional argument overrides the environment, -n reports only
        run(["tmipcrm", "-n", tuxconfig],
            env={k: v for k, v in os.environ.items() if k != "TUXCONFIG"})


if __name__ == "__main__":
    main(globals())
