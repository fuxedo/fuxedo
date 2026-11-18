"""Client-side transactional /Q through TMQUEUE (the platform model).

A client in a global transaction routes its queue operations to the
QSPACE:TMQUEUE server of the QUE group; the group is recorded as a
transaction branch and the group's TMS (.TM) prepares/commits it. No
multi-RM per process anywhere: one RM per server process, coordination
across processes, exactly like the platform.
"""

import os

import tuxedo as t

import test_online_updates as tou
from fuxtest import main, run

if not tou.is_fuxedo():
    print("not a fuxedo installation, skipping client tx /Q tests")
    raise SystemExit(0)

UBB = """*RESOURCES
SECURITY NONE
MASTER tuxapp
MODEL SHM
IPCKEY {ipckey}

*MACHINES
"{host}" LMID=tuxapp APPDIR="{dir}" TUXCONFIG="{dir}/tuxconfig" TUXDIR="{tuxdir}" TLOGDEVICE="{dir}/TLOG"

*GROUPS
QUE LMID=tuxapp GRPNO=2 OPENINFO="TUXEDO/QM:{dir}/QUE:QSPACE" \
	TMSNAME=TMSQM TMSCOUNT=2

*SERVERS
TMQUEUE SRVGRP=QUE SRVID=1 CLOPT="-s QSPACE:TMQUEUE -- "

*SERVICES
"""


def test_client_transactional_enqueue_commit():
    with tou.scratch("ctxq") as dir:
        ubb = UBB.format(ipckey=48311 + os.getpid() % 500,
                         host=__import__("socket").gethostname(), dir=dir,
                         tuxdir=os.environ["TUXDIR"])
        (dir / "TLOG").mkdir()
        # qmxa xa_open validates the qspace directory at tpopen
        (dir / "QUE" / "QSPACE").mkdir(parents=True)
        (dir / "ubbconfig").write_text(ubb)
        env = {"TUXCONFIG": dir / "tuxconfig",
               "FLDTBLDIR32": os.path.join(os.environ["TUXDIR"], "udataobj"),
               "FIELDTBLS32": "tpadm"}
        os.environ["TUXCONFIG"] = str(dir / "tuxconfig")
        run(["crdl", "-o", "0", "-s", "100", dir / "TLOG"], env=env, cwd=dir)
        run(["buildtms", "-o", "TMSQM", "-r", "TUXEDO/QM"], env=env, cwd=dir)
        run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir)
        run(["tmboot", "-y"], env=env, cwd=dir)
        try:
            import time
            deadline = time.time() + 30
            while True:
                try:
                    out = t.tpadmcall({
                        "TA_CLASS": "T_SERVICE", "TA_OPERATION": "GET"}).data
                    if "QSPACE:TMQUEUE" in out.get("TA_SERVICENAME", []):
                        break
                except t.XatmiException:
                    pass
                assert time.time() < deadline, "TMQUEUE did not advertise"
                time.sleep(0.5)

            # enqueue inside a client-side global transaction, commit
            t.tpbegin(30, 0)
            ctl = t.TPQCTL()
            t.tpenqueue("QSPACE", "Q1", ctl, "tx-payload")
            t.tpcommit()

            # the committed message must be visible and no staging left
            ctl2 = t.TPQCTL()
            _, msg = t.tpdequeue("QSPACE", "Q1", ctl2)
            assert msg == "tx-payload", msg

            # abort undoes the staged enqueue
            t.tpbegin(30, 0)
            t.tpenqueue("QSPACE", "Q1", t.TPQCTL(), "aborted-one")
            t.tpabort()
            try:
                t.tpdequeue("QSPACE", "Q1", t.TPQCTL())
                raise AssertionError("aborted message must not be visible")
            except t.XatmiException as e:
                assert e.code == t.TPETIME, e.code
            txroot = dir / "QUE" / "QSPACE" / ".tx"
            assert not txroot.exists() or not any(txroot.iterdir())
        finally:
            run(["tmshutdown", "-y"], env=env, cwd=dir)


if __name__ == "__main__":
    main(globals())
