"""The tuxedo-python demo (demo/py3) end to end against fuxedo.

Copies the demo sources from TUXEDO_PYTHON, boots the domain (MSSQ sets,
SECONDARYRQ singletons, a multithreaded server), exercises buffers, the
HTTP-via-Tuxedo server, .TMIB T_SVCGRP and the in-memory cache services.

The api.py HTTP step depends on the external ECB rates endpoint at call
time; any HTTP response counts as reaching the service (a 500 there means
the external fetch failed, not fuxedo).

fuxedo-only for now (MSSQ/SECONDARYRQ). Run against Oracle separately via
the demo's own Makefile.
"""

import os
import shutil
import time
import socket
import urllib.request

import tuxedo as t

from fuxtest import is_fuxedo, main, run, scratch

if not is_fuxedo():
    print("not a fuxedo installation, skipping demo test")
    raise SystemExit(0)

DEMO = os.path.join(os.environ["TUXEDO_PYTHON"], "demo", "py3")


def prepare(dir):
    for name in ("api.py", "buf.py", "client.py", "ecb.py", "mem.py",
                 "in.ud", "ubbconfig.in", "demo"):
        shutil.copy(os.path.join(DEMO, name), dir / name)
    ubb = (dir / "ubbconfig.in").read_text()
    ubb = (ubb.replace("@TUXDIR@", os.environ["TUXDIR"])
              .replace("@UNAME@", socket.gethostname())
              .replace("@CURDIR@", str(dir)))
    (dir / "ubbconfig").write_text(ubb)
    # the demo's ubbconfig points TUXCONFIG at "<dir>/t"
    tuxconfig = dir / "t"
    run(["tmloadcf", "-y", dir / "ubbconfig"], env={"TUXCONFIG": tuxconfig})
    # api.py hardcodes PORT 8000; use a free port in the copied source so
    # the test does not depend on what else runs on this machine
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    api = (dir / "api.py").read_text().replace("PORT = 8000", f"PORT = {port}")
    (dir / "api.py").write_text(api)
    return tuxconfig, port


def test_demo_end_to_end():
    with scratch("demo") as dir:
        tuxconfig, port = prepare(dir)
        env = {"TUXCONFIG": tuxconfig,
               "FLDTBLDIR32": os.pathsep.join(
                   [os.path.join(os.environ["TUXDIR"], "udataobj"), str(dir)]),
               "FIELDTBLS32": "tpadm,demo"}

        proc = run(["tmunloadcf"], env=env)
        assert "GROUP1" in proc.stdout

        run(["python3", "buf.py"], input=(dir / "in.ud").read_text(), env=env, cwd=dir)

        run(["tmboot", "-y"], env=env, cwd=dir)

        try:
            # HTTP through the Tuxedo stack: api.py (tuxedo server with an
            # embedded HTTP listener) calls GETRATE on the ecb.py servers.
            req = urllib.request.Request(
                "http://localhost:%d/" % port,
                data=b'{"USD": "5"}',
                headers={"Content-Type": "application/json"})
            deadline = time.time() + 15
            while True:
                try:
                    with urllib.request.urlopen(req, timeout=60) as resp:
                        body = resp.read()
                    assert body, "empty reply from api.py"
                    break
                except urllib.error.HTTPError as e:
                    # 500 here means the ECB fetch failed, not fuxedo
                    assert e.code in (200, 500), e.code
                    break
                except urllib.error.URLError:
                    if time.time() > deadline:
                        raise
                    time.sleep(0.5)

            # .TMIB T_SVCGRP (needs TA_SRVGRP), RELOAD_ singletons on the
            # secondary queue, and the multithreaded mem.py cache.
            run(["python3", "client.py"], env=env, cwd=dir)
        finally:
            run(["tmshutdown", "-y"], env=env, cwd=dir)


if __name__ == "__main__":
    main(globals())
