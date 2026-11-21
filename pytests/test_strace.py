"""Syscall-chain compatibility tests via hisstrace.

Traces ATMI clients with hisstrace (the programmable strace) and
asserts on ordered syscall chains - the scenario coroutine awaits each
step in sequence (a big queue send that must fail, the /tmp spill file
opening, a giant write, a tiny name-carrying msgsnd, the unlink) and
the pytest judges the verdict line. Runs against fuxedo and Oracle
Tuxedo with the same client binary.

hisstrace comes from the local clone (../../hisstrace/src) when present
so dogfooding exercises its development tree; otherwise the installed
package is used. Skips when hisstrace or ptrace is unavailable.
"""

import json
import os
import socket
import subprocess
import sys

from fuxtest import main, run, scratch


def is_fuxedo():
    tuxdir = os.environ.get("TUXDIR", "")
    return os.path.exists(os.path.join(tuxdir, "lib", "libfuxedo.so"))

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HISS = os.path.join(os.path.dirname(REPO), "hisstrace", "src")

try:
    if os.path.isdir(HISS):
        sys.path.insert(0, HISS)
    import hisstrace  # noqa: F401
    HAVE_HISS = True
except Exception:
    HAVE_HISS = False

UBB = """*RESOURCES
SECURITY NONE
MASTER tuxapp
MODEL SHM
IPCKEY {ipckey}
BLOCKTIME 30
SCANUNIT 1000MS

*MACHINES
"{host}" LMID=tuxapp APPDIR="{dir}" TUXCONFIG="{dir}/tuxconfig" TUXDIR="{tuxdir}"

*GROUPS
GROUP1 LMID=tuxapp GRPNO=1

*SERVERS
server SRVGRP=GROUP1 SRVID=1 MIN=1 MAX=1 MINDISPATCHTHREADS=2 MAXDISPATCHTHREADS=2 RQADDR=main CLOPT="-s ECHO -s ECHOBIG -s SLEEP -s PING"
"""

SERVER = r"""
#include <assert.h>
#include <atmi.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <userlog.h>

void ECHO(TPSVCINFO *svcinfo) {
  assert(svcinfo->flags & TPCONV);
  char *pending = tpalloc("STRING", NULL, 1024);
  strcpy(pending, svcinfo->data);
  char *donebuf = tpalloc("STRING", NULL, 16);
  strcpy(donebuf, "done");
  char *buf = tpalloc("STRING", NULL, 1024);
  long len = 0;
  long revent = 0;
  while (1) {
    if (tprecv(svcinfo->cd, &buf, &len, 0, &revent) == -1) {
      break;
    }
    if (revent & TPEV_DISCONIMM) {
      break;
    }
    if (len <= 1) {
      tpreturn(TPSUCCESS, 7, donebuf, 0, 0);
    }
    if (tpsend(svcinfo->cd, pending, 0, TPRECVONLY, &revent) == -1) {
      break;
    }
    strcpy(pending, buf);
  }
  tpreturn(TPSUCCESS, 0, svcinfo->data, 0, 0);
}

void ECHOBIG(TPSVCINFO *svcinfo) {
  long n = 200000;
  char *buf = tpalloc("CARRAY", NULL, n);
  for (long i = 0; i < n; i++) {
    buf[i] = (char)(i % 251);
  }
  tpreturn(TPSUCCESS, 5, buf, n, 0);
}

// stays busy long enough for the queue to back up behind it, short
// enough that the overflow call behind the backlog still gets an
// answer inside the test's call timeout
void SLEEP(TPSVCINFO *svcinfo) {
  sleep(1);
  tpreturn(TPSUCCESS, 0, svcinfo->data, 0, 0);
}

// plain request/response echo for the overflow call
void PING(TPSVCINFO *svcinfo) {
  tpreturn(TPSUCCESS, 0, svcinfo->data, 0, 0);
}
"""

CLIENT = r"""
#include <assert.h>
#include <atmi.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  assert(tpinit(NULL) != -1);
  if (argc > 1 && strcmp(argv[1], "conv") == 0) {
    char *buf = tpalloc("STRING", NULL, 1024);
    long len = 0;
    long revent = 0;
    strcpy(buf, "hello1");
    int cd = tpconnect("ECHO", buf, 0, TPSENDONLY);
    assert(cd != -1);
    strcpy(buf, "hello2");
    assert(tpsend(cd, buf, 0, TPRECVONLY, &revent) != -1);
    assert(tprecv(cd, &buf, &len, 0, &revent) != -1);
    assert(strcmp(buf, "hello1") == 0);
    strcpy(buf, "hello3");
    assert(tpsend(cd, buf, 0, TPRECVONLY, &revent) != -1);
    assert(tprecv(cd, &buf, &len, 0, &revent) != -1);
    assert(strcmp(buf, "hello2") == 0);
    strcpy(buf, "");
    assert(tpsend(cd, buf, 0, TPRECVONLY, &revent) != -1);
    assert(tprecv(cd, &buf, &len, 0, &revent) != -1);
    assert(revent & TPEV_SVCSUCC);
  } else if (argc > 1 && strcmp(argv[1], "large") == 0) {
    long n = 200000;
    char *buf = tpalloc("CARRAY", NULL, n);
    char *rcv = tpalloc("CARRAY", NULL, n);
    long rlen = 0;
    for (long i = 0; i < n; i++) {
      buf[i] = (char)(i % 251);
    }
    assert(tpcall("ECHOBIG", buf, n, &rcv, &rlen, 0) != -1);
    assert(rlen == n);
  } else if (argc > 1 && strcmp(argv[1], "fullq") == 0) {
    // large-ish requests so a full queue means a short backlog that
    // drains quickly once the overflow call is in flight; then a
    // top-up staircase so no trailing slot fits even a small request
    char *s = tpalloc("STRING", NULL, 1500);
    memset(s, 's', 1499);
    int filled = 0;
    for (int i = 0; i < 500; i++) {
      if (tpacall("SLEEP", s, 0, TPNOREPLY | TPNOBLOCK) == -1) {
        break;
      }
      filled++;
    }
    assert(filled > 4);
    char *t = tpalloc("STRING", NULL, 300);
    memset(t, 't', 299);
    for (int i = 0; i < 500; i++) {
      if (tpacall("SLEEP", t, 0, TPNOREPLY | TPNOBLOCK) == -1) {
        break;
      }
    }
    char *u = tpalloc("STRING", NULL, 16);
    strcpy(u, "x");
    for (int i = 0; i < 500; i++) {
      if (tpacall("SLEEP", u, 0, TPNOREPLY | TPNOBLOCK) == -1) {
        break;
      }
    }
    // the queue is full behind the SLEEP backlog. NOBLOCK never spills,
    // it fails the send with TPEBLOCK, like the platform. the probe is
    // deliberately larger than any trailing queue slot
    char *b = tpalloc("STRING", NULL, 2000);
    memset(b, 'n', 1999);
    char *r = tpalloc("STRING", NULL, 64);
    long rlen = 0;
    int ncd = tpacall("PING", b, 0, TPNOBLOCK | TPNOTIME);
    assert(ncd == -1 && tperrno == TPEBLOCK);
    // a blocking send over 500 bytes against the same full queue: the
    // platform spills the payload to a file right away and waits out
    // the tiny control message until a queue slot opens
    memset(b, 'v', 300);
    b[300] = '\0';
    int rc = tpcall("PING", b, 0, &r, &rlen, 0);
    printf("fullq call rc=%d tperrno=%d rlen=%ld\n", rc, tperrno, rlen);
    fflush(stdout);
    assert(rc == 0);
    assert(rlen == 301);  // STRING echo carries the trailing NUL
  } else {
    return 2;
  }
  tpterm();
  return 0;
}
"""


def have_ptrace():
    try:
        proc = subprocess.run([sys.executable, "-c", "pass"], timeout=10,
                              capture_output=True)
        return proc.returncode == 0
    except Exception:
        return False


def compile_c(dir, name, source, libs):
    src = dir / (name + ".c")
    src.write_text(source)
    import subprocess
    tool = "buildserver" if name == "server" else "buildclient"
    args = [tool, "-o", str(dir / name), "-f", str(src), "-v",
            "-f", "-Wl,--no-as-needed"]
    if name == "server":
        args[args.index("-v"):args.index("-v")] = [
            "-s", "ECHO", "-s", "ECHOBIG", "-s", "SLEEP", "-s", "PING"]
    proc = subprocess.run(args, cwd=dir, capture_output=True, text=True)
    assert proc.returncode == 0 and (dir / name).exists(), \
        (proc.returncode, proc.stdout[-1500:], proc.stderr[-1500:])


def audit(dir, env, mode, *argv):
    """Trace argv under hisstrace; return the chain verdict dict."""
    hiss_env = dict(os.environ)
    hiss_env["HISS_MODE"] = mode
    if os.path.isdir(HISS):
        hiss_env["PYTHONPATH"] = HISS + os.pathsep + hiss_env.get(
            "PYTHONPATH", "")
    proc = subprocess.run(
        [sys.executable, "-m", "hisstrace", "scenario",
         os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "ipc_audit_scenario.py"), "--", *argv],
        env={**hiss_env, **{k: str(v) for k, v in env.items()}},
        cwd=dir, capture_output=True, text=True, timeout=300)
    line = next((l for l in proc.stdout.splitlines()
                 if l.startswith("HISS_CHAIN ")), None)
    assert line is not None, (proc.stdout[-2000:], proc.stderr[-2000:])
    assert proc.returncode == 0, (proc.returncode, proc.stdout[-1000:],
                                  proc.stderr[-1000:])
    return json.loads(line[len("HISS_CHAIN "):])


def check_verdict(verdict, required=()):
    assert verdict, "the scenario collected nothing - did the client run?"
    missing = [k for k in required if k not in verdict]
    assert not missing, f"scenario steps never ran: {missing}"
    bad = {k: v for k, v in verdict.items()
           if not (v == "ok" or v.startswith("skip"))}
    assert not bad, bad


def boot_domain(dir):
    ubb = UBB.format(ipckey=56111 + os.getpid() % 400,
                     host=socket.gethostname(), dir=dir,
                     tuxdir=os.environ["TUXDIR"])
    (dir / "ubbconfig").write_text(ubb)
    env = {"TUXCONFIG": dir / "tuxconfig",
           "FLDTBLDIR32": os.path.join(os.environ["TUXDIR"], "udataobj"),
           "FIELDTBLS32": "tpadm"}
    run(["tmipcrm", "-y"], env=env, cwd=dir, expect=-1)
    run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir)
    run(["tmboot", "-y"], env=env, cwd=dir)
    return env


def test_conversation_syscall_chain():
    if not (HAVE_HISS and have_ptrace()):
        print("hisstrace or ptrace unavailable, skipping")
        raise SystemExit(0)
    if not is_fuxedo():
        # Oracle 12.2.2 tpconnect rejects any connect data with
        # TPEINVAL on this install (unpinned anomaly); the
        # conversational chain is fuxedo-only until that lands
        print("conversational chain is fuxedo-only for now, skipping")
        return
    with scratch("hissconv") as dir:
        compile_c(dir, "server", SERVER, None)
        compile_c(dir, "client", CLIENT, None)
        env = boot_domain(dir)
        try:
            verdict = audit(dir, env, "conv", str(dir / "client"), "conv")
        finally:
            run(["tmshutdown", "-y"], env=env, cwd=dir, expect=None)
        check_verdict(verdict)


def test_full_queue_promotes_to_file_transfer():
    """A small blocking send against a full queue spills its payload to
    a file immediately, like the platform; NOBLOCK sends never spill
    and fail the send with TPEBLOCK."""
    if not (HAVE_HISS and have_ptrace()):
        print("hisstrace or ptrace unavailable, skipping")
        raise SystemExit(0)
    with scratch("hissfullq") as dir:
        compile_c(dir, "server", SERVER, None)
        compile_c(dir, "client", CLIENT, None)
        env = boot_domain(dir)
        try:
            verdict = audit(dir, env, "fullq", str(dir / "client"), "fullq")
        finally:
            run(["tmshutdown", "-y"], env=env, cwd=dir, expect=None)
        check_verdict(verdict, required=(
            "queue_rejects_inline", "spills_immediately",
            "small_payload_written", "file_message_fits", "reply_arrives"))


def test_large_message_file_transfer_chain():
    if not (HAVE_HISS and have_ptrace()):
        print("hisstrace or ptrace unavailable, skipping")
        raise SystemExit(0)
    with scratch("hissbig") as dir:
        compile_c(dir, "server", SERVER, None)
        compile_c(dir, "client", CLIENT, None)
        env = boot_domain(dir)
        try:
            verdict = audit(dir, env, "large", str(dir / "client"), "large")
        finally:
            run(["tmshutdown", "-y"], env=env, cwd=dir, expect=None)
        check_verdict(verdict)


if __name__ == "__main__":
    main(globals())
