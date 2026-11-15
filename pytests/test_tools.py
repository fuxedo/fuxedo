"""CLI compatibility tests for the smaller fuxedo/Tuxedo tools.

Vendor neutral where possible; ud32 buffer printing is fuxedo-gated because
Oracle ud32 requires a domain to print.
"""

import os
import shutil

from fuxtest import is_fuxedo, main, run, scratch


def test_mkfldhdr32_generates_header():
    tuxdir = os.environ["TUXDIR"]
    with scratch("mkfldhdr32") as dir:
        shutil.copy(os.path.join(tuxdir, "udataobj", "tpadm"), dir / "tpadm")
        run(["mkfldhdr32", "-d", dir, dir / "tpadm"])
        header = (dir / "tpadm.h").read_text()
        assert "#define TA_CLASS" in header
        # field id values are aligned between fuxedo and Oracle
        assert "167778162" in header
        assert "#define SRVCNM" in header


def test_mkfldhdr32_missing_table_fails():
    with scratch("mkfldhdr32-missing") as dir:
        run(["mkfldhdr32", "-d", dir, dir / "nope"], expect=-1)


def test_ud32_help():
    proc = run(["ud32", "-h"], expect=None)
    assert "ud32" in proc.stdout + proc.stderr


def test_ud32_prints_buffer_without_domain():
    if not is_fuxedo():
        print("skipping ud32 buffer print test on non-fuxedo install")
        return
    proc = run(["ud32"], input="SRVCNM\tNO_SUCH_SERVICE\n", expect=None)
    assert "SRVCNM" in proc.stdout
    assert "NO_SUCH_SERVICE" in proc.stdout


def test_tmipcrm_requires_domain():
    env = {k: v for k, v in os.environ.items() if k != "TUXCONFIG"}
    run(["tmipcrm", "-y"], expect=-1, env=env)


def test_buildserver_help():
    proc = run(["buildserver", "-h"], expect=None)
    assert "buildserver" in proc.stdout + proc.stderr


def test_buildclient_help():
    proc = run(["buildclient", "-h"], expect=None)
    assert "buildclient" in proc.stdout + proc.stderr


if __name__ == "__main__":
    main(globals())
