"""dmadmin against a two-domain setup.

Exercises the v1 subset against the gateways' DMADM_<accesspoint>
services: printdomains/printconn show live connection state, disconnect
drops the link, connect restores it.
"""

import os
import re
import time

import test_gwtdomain as tg
from fuxtest import main, run, scratch

if not tg.tou.is_fuxedo():
    print("not a fuxedo installation, skipping dmadmin tests")
    raise SystemExit(0)


def dmadmin(env, script, dir):
    return run(["dmadmin"], input=script + "\nquit\n", env=env, cwd=dir)


def test_printconn_and_connect_disconnect():
    base = 26101 + os.getpid() % 500
    with scratch("dm1") as dir1, scratch("dm2") as dir2:
        env1 = tg.domain(dir1, 50111 + os.getpid() % 500, base,
                         "DOM1", "AA.DOM1", "DOM2", "BB.DOM2", base + 1,
                         "TOUPPER", "TOLOWER", "upper")
        env2 = tg.domain(dir2, 50611 + os.getpid() % 500, base + 1,
                         "DOM2", "BB.DOM2", "DOM1", "AA.DOM1", base,
                         "TOLOWER", "TOUPPER", "lower")
        try:
            tg.wait_for_link(env1, env2)

            out = dmadmin(env1, "printconn", dir1).stdout
            assert "DOM2" in out and "UP" in out, out
            assert re.search(r"DOM2\s+BB\.DOM2\s+//localhost:\d+\s+"
                             r"ON_STARTUP\s+UP", out), out

            out = dmadmin(env1, "printdomains", dir1).stdout
            assert "DOM1" in out and "AA.DOM1" in out, out
            assert "GW1" in out, out

            out = dmadmin(env1, "disconnect DOM2", dir1).stdout
            assert "DOM2" in out, out
            out = dmadmin(env1, "printconn", dir1).stdout
            assert re.search(r"DOM2\s+.*DOWN", out), out

            dmadmin(env1, "connect DOM2", dir1)
            deadline = time.time() + 30
            while True:
                out = dmadmin(env1, "printconn", dir1).stdout
                if re.search(r"DOM2\s+.*UP", out):
                    break
                assert time.time() < deadline, out
                time.sleep(1)

            # unknown access point is rejected without a call
            out = dmadmin(env1, "connect NOPE", dir1).stderr
            assert "NOPE" in out, out
        finally:
            run(["tmshutdown", "-y"], env=env1, cwd=dir1, expect=None)
            run(["tmshutdown", "-y"], env=env2, cwd=dir2, expect=None)


if __name__ == "__main__":
    main(globals())
