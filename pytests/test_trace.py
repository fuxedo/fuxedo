"""Dye tracing parity with the platform.

With changetrace set on the server and TMTRACE on the client, both sides
emit one "TRACE:at:" line per observable API event in the
"{ name(args)" / "} name = result" shape; with tracing off nothing is
logged. The test pins fuxedo's side of that contract.
"""

import os

import tuxedo as t

import test_online_updates as tou
from fuxtest import main, run

if not tou.is_fuxedo():
    print("not a fuxedo installation, skipping trace tests")
    raise SystemExit(0)


def test_dye_trace_on_and_off():
    with tou.scratch("trace") as dir:
        env = tou.boot(dir)
        try:
            # tracing off: a call produces no TRACE lines at all
            assert tou.call("ECHO") == "hi"
            ulog = " ".join(p.read_text() for p in dir.glob("ULOG.*"))
            assert "TRACE:" not in ulog

            # server side on via changetrace; the traced call runs in a
            # fresh client process because TMTRACE is read at process
            # start, like the platform
            tou.tmadmin(env, 'changetrace -m all "*:ulog:dye"', dir)
            env["TMTRACE"] = "*:ulog:dye"
            run(["python3", "-c",
                 "import os,sys; os.environ['TUXCONFIG']=sys.argv[1];"
                 "import tuxedo as t;"
                 "assert t.tpcall('ECHO', 'hi').data == 'hi'",
                 str(dir / "tuxconfig")],
                env=env, cwd=dir)

            ulog = " ".join(p.read_text() for p in dir.glob("ULOG.*"))
            assert 'TRACE:at: { tpservice({"ECHO"' in ulog
            assert "TRACE:at: { tpreturn(" in ulog
            assert "TRACE:at: } tpservice" in ulog
            assert 'TRACE:at: { tpcall("ECHO"' in ulog
            assert "TRACE:at: } tpcall = 0" in ulog

            tou.tmadmin(env, "changetrace -m all off", dir)
            env.pop("TMTRACE", None)
            before = sum(len(p.read_text()) for p in dir.glob("ULOG.*"))
            assert tou.call("ECHO") == "hi"
            after = sum(len(p.read_text()) for p in dir.glob("ULOG.*"))
            assert after - before < 200  # no per-call trace volume
        finally:
            env.pop("TMTRACE", None)
            run(["tmshutdown", "-y"], env=env, cwd=dir)


if __name__ == "__main__":
    main(globals())
