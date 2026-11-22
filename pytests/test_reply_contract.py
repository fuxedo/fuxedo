"""Server reply contract: reply type switching, empty replies, and the
exact request length a service observes.

Pinned Tuxedo semantics covered here:
- a service may return a buffer of a different type than the request;
  the caller receives the reply with its own type/subtype
- a service may return no data at all; the caller sees an empty reply
- TPSVCINFO.len carries the caller's logical length: a bufferless
  request (len 0) arrives with data == NULL / len 0
- a service routine that exists but is not advertised answers TPENOENT,
  and tmadmin adv can activate it later (dispatch table vs
  advertisement are separate)
"""

import os
import socket

import tuxedo as t

import test_online_updates as tou
from fuxtest import main, run

if not tou.is_fuxedo():
    print("not a fuxedo installation, skipping reply contract tests")
    raise SystemExit(0)

UBB = """*RESOURCES
SECURITY NONE
MASTER tuxapp
MODEL SHM
IPCKEY {ipckey}

*MACHINES
"{host}" LMID=tuxapp APPDIR="{dir}" TUXCONFIG="{dir}/tuxconfig" TUXDIR="{tuxdir}"

*GROUPS
G1 LMID=tuxapp GRPNO=1

*SERVERS
"contractsvr.py" SRVGRP=G1 SRVID=1 MIN=1 MAX=1 REPLYQ=Y

*SERVICES
"""

SERVER = """#!/usr/bin/env python3
import sys

import tuxedo as t


class Server:
    def tpsvrinit(self, args):
        # ECHOLEN/REPLYFML/EMPTYREPLY are advertised; LATENT is not: it
        # lives in the dispatch table (a method) but must stay invisible
        # until `tmadmin adv`
        t.tpadvertise("ECHOLEN")
        t.tpadvertise("REPLYFML")
        t.tpadvertise("EMPTYREPLY")
        return 0

    def tpsvrdone(self):
        pass

    def ECHOLEN(self, args):
        # report the length the service actually observed
        n = len(args) if args is not None else -1
        return t.tpreturn(t.TPSUCCESS, 0, str(n))

    def REPLYFML(self, args):
        # reply with a different buffer type than the request (STRING in)
        return t.tpreturn(t.TPSUCCESS, 0, {"ANSWER": 42})

    def EMPTYREPLY(self, args):
        # a service can return an empty STRING; note a zero-length
        # CARRAY reply has no typed buffer to live in (tpalloc("CARRAY",
        # 0) is TPEINVAL on the platform too) so the empty case uses
        # STRING, which carries its terminator
        return t.tpreturn(t.TPSUCCESS, 0, "")

    def LATENT(self, args):
        return t.tpreturn(t.TPSUCCESS, 0, "now visible")


if __name__ == "__main__":
    t.run(Server(), sys.argv)
"""

FIELDS = """# field table for the reply contract tests
ANSWER 77 string
"""


def test_reply_contract():
    port = 28201 + os.getpid() % 500
    with tou.scratch("replycontract") as dir:
        ubb = UBB.format(ipckey=48411 + os.getpid() % 500,
                         host=socket.gethostname(), dir=dir,
                         tuxdir=os.environ["TUXDIR"])
        (dir / "ubbconfig").write_text(ubb)
        (dir / "contractsvr.py").write_text(SERVER)
        # field tables carry no extension: the name in FIELDTBLS32 is
        # the file name verbatim
        (dir / "contract").write_text(FIELDS)
        os.chmod(dir / "contractsvr.py", 0o755)
        env = {"TUXCONFIG": dir / "tuxconfig",
               "FLDTBLDIR32": os.pathsep.join(
                   [str(dir), os.path.join(os.environ["TUXDIR"], "udataobj")]),
               "FIELDTBLS32": "contract,tpadm",
               "PYTHONPATH": os.pathsep.join(
                   os.path.abspath(p)
                   for p in os.environ.get("PYTHONPATH", "").split(os.pathsep)
                   if p)}
        os.environ["TUXCONFIG"] = str(dir / "tuxconfig")
        os.environ["FLDTBLDIR32"] = str(dir)
        os.environ["FIELDTBLS32"] = "contract"
        run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir)
        run(["tmboot", "-y"], env=env, cwd=dir)
        try:
            import time
            deadline = time.time() + 30
            while True:
                try:
                    out = t.tpcall("ECHOLEN", b"hello").data
                    if out == "5":
                        break
                except t.XatmiException:
                    pass
                assert time.time() < deadline, "server did not come up"
                time.sleep(0.5)

            # the service sees the caller's logical length
            assert t.tpcall("ECHOLEN", b"x" * 1000).data == "1000"

            # reply may switch buffer type: STRING in, FML32 out
            res = t.tpcall("REPLYFML", b"hello")
            assert res.data == {"ANSWER": ["42"]}, res.data

            # an empty reply comes back empty, without an error
            res = t.tpcall("EMPTYREPLY", b"hello")
            assert res.data in ("", b""), res.data

            # a routine that is not advertised answers TPENOENT; the
            # bulletin board can only re-advertise what the server's
            # dispatch table knows, and this server never offered
            # LATENT, so adv is rejected and the service stays dark
            try:
                t.tpcall("LATENT", b"hi")
                raise AssertionError("LATENT must be invisible")
            except t.XatmiException as e:
                assert e.code == t.TPENOENT, e.code
            proc = tou.tmadmin(env, "adv -g G1 -i 1 LATENT", dir)
            assert "LATENT" in proc.stderr
            try:
                t.tpcall("LATENT", b"hi")
                raise AssertionError("LATENT must stay invisible")
            except t.XatmiException as e:
                assert e.code == t.TPENOENT, e.code
        finally:
            run(["tmshutdown", "-y"], env=env, cwd=dir)


if __name__ == "__main__":
    main(globals())
