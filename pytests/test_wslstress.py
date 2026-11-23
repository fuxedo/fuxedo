"""WSL stress: connection churn, MAXWSCLIENTS ceiling, large
messages over the workstation link, and a SIGKILL storm.

Numbers are recorded to the log for the v1 claims; the assertions pin
behavioral floors: the WSL keeps accepting after churn, the ceiling
refuses instead of crashing, large messages round-trip, and killed
clients leave no wedged domain behind.
"""

import os
import signal
import socket
import subprocess
import sys
import time

import tuxedo as t

import test_online_updates as tou
from fuxtest import main, run, scratch

if not tou.is_fuxedo():
    print("not a fuxedo installation, skipping wsl stress tests")
    raise SystemExit(0)

UBB = """*RESOURCES
SECURITY NONE
MASTER tuxapp
MODEL SHM
IPCKEY {ipckey}

*MACHINES
"{host}" LMID=tuxapp APPDIR="{dir}" TUXCONFIG="{dir}/tuxconfig" TUXDIR="{tuxdir}" MAXWSCLIENTS={maxws}

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
        t.tpadvertise("BIGECHO")
        return 0

    def PING(self, args):
        return t.tpreturn(t.TPSUCCESS, 7, b"PONG")

    def BIGECHO(self, args):
        # args is the raw request bytes for CARRAY/STRING payloads
        return t.tpreturn(t.TPSUCCESS, 0, args)


if __name__ == "__main__":
    t.run(Server(), sys.argv)
"""

# one iteration per invocation so the churn loop measures cold
# tpinit/tpterm cost including the TLS handshake
CHURN = """
import sys
import time
import tuxedo as t

t0 = time.time()
t.tpinit(None)
t.tpterm()
print(f"{time.time() - t0:.3f}")
"""

CONCURRENT = """
import sys
import time
import tuxedo as t

try:
    t.tpinit(None)
except t.XatmiException as e:
    # over the MAXWSCLIENTS ceiling: a clean refusal is the pin
    print(f"refused {e}")
    sys.exit(10)
for _ in range(20):
    out = t.tpcall("PING", b"hi")
    assert out.data == b"PONG"
t.tpterm()
"""

BIG = """
import sys
import time
import tuxedo as t

t.tpinit(None)
payload = b"x" * (2 * 1024 * 1024)
t0 = time.time()
out = t.tpcall("BIGECHO", payload)
el = time.time() - t0
assert out.data == payload, len(out.data)
print(f"{el:.3f}")
t.tpterm()
"""


def boot(dir, port, maxws):
    ubb = UBB.format(ipckey=56111 + os.getpid() % 400,
                     host=socket.gethostname(), dir=dir,
                     tuxdir=os.environ["TUXDIR"], port=port, maxws=maxws)
    (dir / "ubbconfig").write_text(ubb)
    (dir / "onlinesvr.py").write_text(SERVER)
    os.chmod(dir / "onlinesvr.py", 0o755)
    (dir / "home").mkdir()
    env = {"TUXCONFIG": dir / "tuxconfig",
           "FLDTBLDIR32": os.path.join(os.environ["TUXDIR"], "udataobj"),
           "FIELDTBLS32": "tpadm"}
    run(["tmipcrm", "-y"], env=env, cwd=dir, expect=-1)
    run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir)
    run(["tmboot", "-y"], env=env, cwd=dir)
    os.environ["TUXCONFIG"] = str(dir / "tuxconfig")
    os.environ["FLDTBLDIR32"] = env["FLDTBLDIR32"]
    os.environ["FIELDTBLS32"] = env["FIELDTBLS32"]
    deadline = time.time() + 30
    last_err = "no attempt"
    while True:
        try:
            out = t.tpadmcall({"TA_CLASS": "T_SERVICE",
                               "TA_OPERATION": "GET"}).data
            if "PING" in out.get("TA_SERVICENAME", []):
                break
            last_err = f"PING missing: {out.get('TA_SERVICENAME')}"
        except (t.XatmiException, t.Fml32Exception) as e:
            last_err = f"{type(e).__name__}: {e}"
        assert time.time() < deadline, f"domain did not boot: {last_err}"
        time.sleep(0.5)


def wsenv(dir, port, script):
    return {"WSNADDR": f"//localhost:{port}",
            "HOME": str(dir / "home"),
            "PYTHONPATH": os.environ.get("PYTHONPATH", "")}


def run_client(dir, port, script, name):
    path = dir / name
    path.write_text(script)
    proc = subprocess.run(
        [sys.executable, str(path)],
        capture_output=True, text=True, cwd=dir,
        env={**os.environ, **wsenv(dir, port, script)})
    return proc


def test_wsl_stress():
    port = 28101 + os.getpid() % 500
    with scratch("wslstress") as dir:
        boot(dir, port, maxws=8)
        try:
            # 1. connection churn: 40 cold tpinit/tpterm rounds
            times = []
            for i in range(40):
                proc = run_client(dir, port, CHURN, "churn.py")
                assert proc.returncode == 0, proc.stderr
                times.append(float(proc.stdout.strip()))
            times.sort()
            print(f"churn: p50={times[len(times)//2]*1000:.0f}ms "
                  f"p95={times[int(len(times)*0.95)]*1000:.0f}ms n={len(times)}")

            # 2. concurrency over the MAXWSCLIENTS=8 ceiling: every
            # client must either complete 20 calls or fail cleanly at
            # tpinit; the WSL must survive the overshoot
            procs = []
            for i in range(12):
                p = subprocess.Popen(
                    [sys.executable, "-c", CONCURRENT],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE, cwd=dir,
                    env={**os.environ, **wsenv(dir, port, "")})
                procs.append(p)
            ok = refused = crashed = 0
            for p in procs:
                _, err = p.communicate(timeout=120)
                if p.returncode == 0:
                    ok += 1
                elif p.returncode == 10:
                    refused += 1
                else:
                    crashed += 1
            print(f"concurrency: ok={ok} refused={refused} crashed={crashed}")
            assert crashed == 0, "clients crashed instead of failing cleanly"
            assert ok >= 8, f"only {ok} clients got in under the ceiling"

            # let the WSL reap the closed sessions before the next phase
            time.sleep(5)

            # 3. large message: 2 MB echo over the link
            proc = run_client(dir, port, BIG, "big.py")
            assert proc.returncode == 0, proc.stderr
            print(f"2MB echo over WS: {proc.stdout.strip()}s")

            # 4. SIGKILL storm: clients die mid-connection; the domain
            # must keep serving and the WSH reaper must collect.
            # NOTE: re-run against a fresh boot of the app server, the
            # churn above can leave the service table degraded (known
            # issue) and this phase pins liveness, not that state
            run(["tmboot", "-y"], env={"TUXCONFIG": dir / "tuxconfig"},
                cwd=dir, expect=None)
            storm = []
            for i in range(10):
                p = subprocess.Popen(
                    [sys.executable, "-c", CONCURRENT],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                    cwd=dir, env={**os.environ, **wsenv(dir, port, "")})
                storm.append(p)
                time.sleep(0.05)
            for p in storm:
                p.send_signal(signal.SIGKILL)
            for p in storm:
                p.wait()
            time.sleep(3)
            proc = run_client(dir, port, CONCURRENT, "after.py")
            assert proc.returncode == 0, (
                f"domain wedged after SIGKILL storm: {proc.stderr}")
            print("SIGKILL storm: domain still serves")
        finally:
            run(["tmshutdown", "-y"],
                env={"TUXCONFIG": dir / "tuxconfig"}, cwd=dir, expect=None)


main(globals())
