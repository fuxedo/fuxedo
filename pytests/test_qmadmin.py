"""qmadmin(1) CLI tests plus filesystem interop with the python module.

fuxedo-specific: Oracle qmadmin manages raw devices and cannot operate on
queue space folders. Skips (exit 0) when TUXDIR points at Oracle Tuxedo.
"""

import os
import time

import tuxedo as t

from fuxtest import is_fuxedo, main, run, scratch

if not is_fuxedo():
    print("not a fuxedo installation, skipping qmadmin tests")
    raise SystemExit(0)


def qmadmin(dir, script):
    return run(["qmadmin"], input=script,
               env={"QMCONFIG": dir / "qdevice"})


def test_create_open_close_delete():
    with scratch("qmadmin-space") as dir:
        proc = qmadmin(dir, "\n".join([
            "qspacecreate QS1 42000 5000 30 20 30 20000 ERRORQ y 16",
            "qopen QS1",
            "qclose",
            "qspacedelete -y QS1",
            "q",
        ]) + "\n")
        assert "Queue space QS1 created" in proc.stdout
        assert "Copyright" not in proc.stdout
        assert "Queue space QS1 deleted" in proc.stdout
        assert not (dir / "qdevice" / "QS1").exists()


def test_queue_lifecycle():
    with scratch("qmadmin-queue") as dir:
        qmadmin(dir, "qspacecreate QS1 42000 500 10 5 5 100 ERRORQ y 16\nqopen QS1\nqcreate Q1 priority top,msgid 1 30 100m 50m\nq\n")
        assert (dir / "qdevice" / "QS1" / "Q1" / ".meta").exists()
        proc = qmadmin(dir, "qopen QS1\nqset Q1\nqinfo\nqlist\nq\n")
        assert "Queue Q1: 0 messages" in proc.stdout
        assert "Q1 (0 messages)" in proc.stdout
        proc = qmadmin(dir, "qopen QS1\nqdelete -y Q1\nqlist\nq\n")
        assert "Queue Q1 deleted" in proc.stdout
        assert "Q1 (0 messages)" not in proc.stdout


def test_emulated_device_commands():
    with scratch("qmadmin-dev") as dir:
        proc = qmadmin(dir, "crdl /dev/rawfs 0 80000\ncrdl /home/queues/FS 0 600\nlidl\nqprinttrans\nq\n")
        assert "Created device /dev/rawfs, offset 0, size 80000" in proc.stdout
        assert "device 1:" in proc.stdout
        assert "No transactions found" in proc.stdout


def test_message_written_by_python_is_visible():
    with scratch("qmadmin-py") as dir:
        qmadmin(dir, "qspacecreate QS1 42000 500 10 5 5 100 ERRORQ\nqopen QS1\nqcreate Q1\nq\n")
        device = dir / "qdevice"
        os.environ["QMCONFIG"] = str(device)

        # enqueue through the python module
        t.tpenqueue("QS1", "Q1", t.TPQCTL(), {"TA_OPERATION": ["from-python"]})

        queue = device / "QS1" / "Q1"
        messages = [f for f in queue.iterdir() if not f.name.startswith(".")]
        assert len(messages) == 1

        proc = qmadmin(dir, "qopen QS1\nqset Q1\nqscan\nqprint {id}\nqdeletemsg -y {id}\nqscan\nq\n".format(id=messages[0].name))
        assert "prio=50" in proc.stdout
        assert "Message id: " + messages[0].name in proc.stdout
        assert "payload: " in proc.stdout
        assert "Message " + messages[0].name + " deleted" in proc.stdout

        proc = qmadmin(dir, "qopen QS1\nqset Q1\nqscan\nq\n")
        assert "prio=50" not in proc.stdout

        # and the python module can read messages written by qmadmin users:
        # enqueue a plain-text message file the way the C++ tests do
        header = ("FUXQ1\ncorrid:\nreplyq:\nfailq:\npriority:50\n"
                  "enq:{0}\ndeq:0\nexp:0\n\n".format(int(time.time())))
        exported = t.tpexport({"TA_OPERATION": ["from-qmadmin"]})
        (queue / "050.0000000000000099").write_bytes(
            header.encode() + exported)
        ctl, data = t.tpdequeue("QS1", "Q1", t.TPQCTL())
        assert data == {"TA_OPERATION": ["from-qmadmin"]}


def test_dot_prefixed_names_rejected():
    with scratch("qmadmin-dot") as dir:
        proc = qmadmin(dir, "qspacecreate .bad 1 1 1 1 1 1\nq\n")
        assert "QMEINVAL" in proc.stdout or "created" not in proc.stdout


if __name__ == "__main__":
    main(globals())
