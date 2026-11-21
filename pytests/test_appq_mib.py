"""/Q MIB classes: T_APPQSPACE / T_APPQ / T_APPQMSG GET.

fuxedo-only: the classes are served from the folder-based queue device,
which has no Oracle counterpart to cross-test against. The shape mirrors
the customer installer flow (impex.py): read the space and queue
definitions back through .TMIB, and look at individual messages -
including ones whose deq_time is in the future, which tpenqueue/tpdequeue
hide but the MIB shows (TA_STATE "TIME").
"""

import os
import socket

os.environ.setdefault("FLDTBLDIR32",
                      os.path.join(os.environ["TUXDIR"], "udataobj"))
os.environ.setdefault("FIELDTBLS32", "tpadm")

import tuxedo as t

from fuxtest import is_fuxedo, main, run, scratch

if not is_fuxedo():
    print("not a fuxedo installation, skipping /Q MIB tests")
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
"""


def qmadmin(device, script):
    run(["qmadmin", device], input=script + "\nq\n")


def boot(dir):
    ubb = UBB.format(ipckey=58111 + os.getpid() % 300,
                     host=socket.gethostname(), dir=dir,
                     tuxdir=os.environ["TUXDIR"])
    (dir / "ubbconfig").write_text(ubb)
    pre = {"TUXCONFIG": dir / "tuxconfig"}
    run(["tmshutdown", "-y"], env=pre, cwd=dir, expect=-1)
    run(["tmipcrm", "-y"], env=pre, cwd=dir, expect=-1)
    env = {"TUXCONFIG": dir / "tuxconfig"}
    run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir)
    os.environ["TUXCONFIG"] = str(dir / "tuxconfig")
    return env


def mibget(klass, device, **filters):
    req = {"TA_CLASS": klass, "TA_OPERATION": "GET", "TA_QMCONFIG": device}
    req.update(filters)
    return t.tpadmcall(req).data


def test_appq_mib():
    with scratch("appqmib") as dir:
        boot(dir)
        device = str(dir / "QUE")

        # one session: qopen state lives only within a qmadmin process
        qmadmin(device, """qspacecreate QS1 4445 200 12 7 5 11 errq n 16
qopen QS1
qcreate JOBS fifo none 2 30 100% 0% -""")

        os.environ["QMCONFIG"] = device
        t.tpenqueue("QS1", "JOBS", t.TPQCTL(), "run-now")
        t.tpenqueue("QS1", "JOBS", t.TPQCTL(flags=t.TPQTIME_REL, deq_time=600),
                    "run-later")

        spaces = mibget("T_APPQSPACE", device)
        assert spaces["TA_APPQSPACENAME"] == ["QS1"]
        assert spaces["TA_ERRORQNAME"] == ["errq"]
        assert spaces["TA_MAXMSG"] == [11]
        assert spaces["TA_STATE"] == ["ACT"]

        queues = mibget("T_APPQ", device)
        assert queues["TA_APPQNAME"] == ["JOBS"]
        assert queues["TA_APPQORDER"] == ["fifo"]
        assert queues["TA_OUTOFORDER"] == ["none"]
        assert queues["TA_CURQUEUES"] == [2]

        msgs = mibget("T_APPQMSG", device)
        assert msgs["TA_OCCURS"] == [2]
        states = sorted(msgs["TA_STATE"])
        assert states == ["READY", "TIME"], \
            "future message must be visible as TIME through the MIB"
        assert msgs["TA_PRIORITY"] == [50, 50]

        # per-message filter by queue name
        filtered = mibget("T_APPQMSG", device, TA_APPQNAME="JOBS")
        assert filtered["TA_OCCURS"] == [2]

        # the future message is not dequeuable yet, the ready one is
        _, data = t.tpdequeue("QS1", "JOBS", t.TPQCTL())
        assert data == "run-now"
        from tuxedo import XatmiException
        try:
            t.tpdequeue("QS1", "JOBS", t.TPQCTL())
            raise AssertionError("future message must not be dequeuable yet")
        except XatmiException as e:
            assert e.code == t.TPETIME, f"unexpected code {e.code}"


if __name__ == "__main__":
    main(globals())
