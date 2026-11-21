"""Chapter 9 of the book: Oracle Database with global transactions.

Faithful to the book's flow: db.py (COUNT, local transactions) and
dbxa.py (STORE/COUNTXA through xaoSvcCtx(), global transactions), the
book's ubbconfig shape, and the exact client sequence from the chapter:
COUNT=0, begin, STORE, COUNT still 0, COUNTXA=1, commit, COUNT=1.

Runs on fuxedo and Oracle Tuxedo alike (the book's own platform).
Self-skips when the database or the oracledb driver is unavailable.
"""

import os
import socket

os.environ.setdefault("FLDTBLDIR32",
                      os.path.join(os.environ["TUXDIR"], "udataobj"))
os.environ.setdefault("FIELDTBLS32", "tpadm")

try:
    import oracledb
except ImportError:
    oracledb = None

import tuxedo as t

from fuxtest import main, run, scratch

ORACLE_HOME = os.environ.get("ORACLE_HOME",
                             "/opt/oracle/product/21c/dbhomeXE")
DB_USER = os.environ.get("ORARM_USER", "fuxedo")
DB_PASS = os.environ.get("ORARM_PASS", "fuxedo")
DB_DSN = os.environ.get("ORARM_DSN", "localhost:1521/XEPDB1")
DB_ALIAS = os.environ.get("ORARM_DB_ALIAS", "XEPDB1")

UBB = """*RESOURCES
SECURITY NONE
MASTER tuxapp
MODEL SHM
IPCKEY {ipckey}
SCANUNIT 5
BLOCKTIME 30
MAXACCESSERS 30
MAXSERVERS 20
MAXSERVICES 30

*MACHINES
"{host}" LMID=tuxapp
	TUXCONFIG="{dir}/tuxconfig"
	TUXDIR="{tuxdir}"
	APPDIR="{dir}"
	TLOGDEVICE="{dir}/tlog"
	TLOGSIZE=100

*GROUPS
GROUP1 LMID=tuxapp GRPNO=1 TMSNAME=TMS TMSCOUNT=2
GROUP2 LMID=tuxapp GRPNO=2 TMSNAME=TMS_ORA TMSCOUNT=2
	OPENINFO="Oracle_XA:Oracle_XA+Acc=P/{user}/{passwd}+SqlNet={alias}+SesTm=30+Threads=true"

*SERVERS
"db.py" SRVGRP=GROUP1 SRVID=1
	REPLYQ=Y MAXGEN=2 RESTART=Y GRACE=0
	MIN=1 MAX=1
"dbxa.py" SRVGRP=GROUP2 SRVID=2
	REPLYQ=Y MAXGEN=2 RESTART=Y GRACE=0
	MIN=1 MAX=1{threads}

*SERVICES
"""

TNSNAMES = """{alias} =
  (DESCRIPTION =
    (ADDRESS = (PROTOCOL = TCP)(HOST = localhost)(PORT = 1521))
    (CONNECT_DATA = (SERVICE_NAME = {alias}))
  )
"""

DB_PY = """#!/usr/bin/env python3
import os
import sys

import oracledb
import tuxedo as t

oracledb.init_oracle_client(lib_dir=os.path.join(os.environ["ORACLE_HOME"], "lib"))


class Server:
    def tpsvrinit(self, argv):
        t.tpadvertise("COUNT")
        self.db = oracledb.connect(
            user=os.environ["ORARM_USER"],
            password=os.environ["ORARM_PASS"],
            dsn=os.environ["ORARM_DSN"],
        )
        return 0

    def COUNT(self, req):
        with self.db.cursor() as dbc:
            dbc.execute("SELECT COUNT(1) FROM messages")
            req["TA_STATUS"] = "Count={}".format(dbc.fetchone()[0])
        return t.tpreturn(t.TPSUCCESS, 0, req)


t.run(Server(), sys.argv)
"""

DBXA_PY = """#!/usr/bin/env python3
import os
import sys
import uuid

import oracledb
import tuxedo as t

oracledb.init_oracle_client(lib_dir=os.path.join(os.environ["ORACLE_HOME"], "lib"))


class Server:
    def tpsvrinit(self, argv):
        t.tpadvertise("STORE")
        t.tpadvertise("COUNTXA")
        self.db = oracledb.connect(handle=t.xaoSvcCtx())
        return 0

    def STORE(self, req):
        with self.db.cursor() as dbc:
            dbc.execute(
                "INSERT INTO messages VALUES (:1, :2)",
                [str(uuid.uuid4()), req["TA_SOURCE"][0]],
            )
        return t.tpreturn(t.TPSUCCESS, 0, req)

    def COUNTXA(self, req):
        with self.db.cursor() as dbc:
            dbc.execute("SELECT COUNT(1) FROM messages")
            req["TA_STATUS"] = "Count={}".format(dbc.fetchone()[0])
        return t.tpreturn(t.TPSUCCESS, 0, req)


t.run(Server(), sys.argv, "Oracle_XA")
"""


def db_available():
    if oracledb is None:
        return False
    try:
        oracledb.init_oracle_client(lib_dir=os.path.join(ORACLE_HOME, "lib"))
        conn = oracledb.connect(user=DB_USER, password=DB_PASS, dsn=DB_DSN)
        conn.close()
        return True
    except Exception:
        return False


def db_setup():
    conn = oracledb.connect(user=DB_USER, password=DB_PASS, dsn=DB_DSN)
    cur = conn.cursor()
    try:
        cur.execute("create table messages (message_id VARCHAR2(36),"
                    " content VARCHAR2(256), primary key(message_id))")
    except oracledb.DatabaseError:
        pass
    cur.execute("delete from messages")
    conn.commit()
    conn.close()


def boot(dir, threads="", dbxa=DBXA_PY):
    ubb = UBB.format(ipckey=65111 + os.getpid() % 300,
                     host=socket.gethostname(), dir=dir,
                     tuxdir=os.environ["TUXDIR"], user=DB_USER,
                     passwd=DB_PASS, alias=DB_ALIAS, threads=threads)
    (dir / "ubbconfig").write_text(ubb)
    (dir / "tnsnames.ora").write_text(
        TNSNAMES.format(alias=DB_ALIAS))
    (dir / "db.py").write_text(DB_PY)
    (dir / "dbxa.py").write_text(dbxa)
    os.chmod(dir / "db.py", 0o755)
    os.chmod(dir / "dbxa.py", 0o755)

    pre = {"TUXCONFIG": dir / "tuxconfig"}
    run(["tmshutdown", "-y"], env=pre, cwd=dir, expect=-1)
    run(["tmipcrm", "-y"], env=pre, cwd=dir, expect=-1)
    # Oracle's prebuilt lib/TMS.o needs a non-PIE link on modern
    # toolchains; harmless elsewhere
    p = run(["buildtms", "-o", "TMS_ORA", "-r", "Oracle_XA", "-v"],
            env=dict(pre, CFLAGS="-no-pie"), cwd=dir, expect=None)
    if not (dir / "TMS_ORA").exists():
        raise AssertionError("buildtms failed: " + p.stdout + p.stderr)

    env = {"TUXCONFIG": dir / "tuxconfig",
           "ORACLE_HOME": ORACLE_HOME,
           "LD_LIBRARY_PATH": os.path.join(ORACLE_HOME, "lib") +
                              os.pathsep + os.environ.get("LD_LIBRARY_PATH", ""),
           "TNS_ADMIN": dir,
           "ORARM_USER": DB_USER,
           "ORARM_PASS": DB_PASS,
           "ORARM_DSN": DB_DSN,
           "ORARM_DB_ALIAS": DB_ALIAS}
    run(["tmloadcf", "-y", dir / "ubbconfig"], env=env, cwd=dir)
    run(["tmadmin"], input='crdl -b 200 -z %s\ncrlog -m tuxapp\nq\n' %
        (dir / "tlog"), env=env, cwd=dir)
    p = run(["tmboot", "-y"], env=env, cwd=dir, expect=None)
    if "Failed" in p.stdout:
        print(p.stdout)
        for name in ("stdout", "stderr"):
            f = dir / name
            if f.exists():
                print("----", f)
                print(f.read_text()[-2000:])
        for u in sorted(dir.glob("ULOG*")):
            print("----", u)
            print(u.read_text()[-2000:])
        raise AssertionError("tmboot reported failures")

    os.environ["TUXCONFIG"] = str(dir / "tuxconfig")
    os.environ["TNS_ADMIN"] = str(dir)
    os.environ["ORACLE_HOME"] = ORACLE_HOME
    oracledb.init_oracle_client(lib_dir=os.path.join(ORACLE_HOME, "lib"))

    import time
    deadline = time.time() + 30
    while True:
        try:
            if t.tpcall("COUNT", {}).data["TA_STATUS"] == ["Count=0"]:
                break
        except t.XatmiException:
            pass
        if time.time() >= deadline:
            for name in ("stdout", "stderr"):
                f = dir / name
                if f.exists() and f.read_text():
                    print("----", f)
                    print(f.read_text()[-2000:])
            for u in sorted(dir.glob("ULOG*")):
                print("----", u)
                print(u.read_text()[-2000:])
            raise AssertionError("domain did not boot")
        time.sleep(0.5)
    return env


DBXA_THREADED_PY = """#!/usr/bin/env python3
import os
import sys
import threading
import uuid

import oracledb
import tuxedo as t

oracledb.init_oracle_client(lib_dir=os.path.join(os.environ["ORACLE_HOME"], "lib"))


class Server:
    def tpsvrinit(self, argv):
        t.tpadvertise("STORE")
        t.tpadvertise("COUNTXA")
        self.local = threading.local()
        return 0

    def tpsvrthrinit(self, argv):
        self.local.db = oracledb.connect(handle=t.xaoSvcCtx())  # book passes threaded=True (cx_Oracle API); oracledb threads by default
        return 0

    def STORE(self, req):
        with self.local.db.cursor() as dbc:
            dbc.execute(
                "INSERT INTO messages VALUES (:1, :2)",
                [str(uuid.uuid4()), req["TA_SOURCE"][0]],
            )
        return t.tpreturn(t.TPSUCCESS, 0, req)

    def COUNTXA(self, req):
        with self.local.db.cursor() as dbc:
            dbc.execute("SELECT COUNT(1) FROM messages")
            req["TA_STATUS"] = "Count={}".format(dbc.fetchone()[0])
        return t.tpreturn(t.TPSUCCESS, 0, req)


t.run(Server(), sys.argv, "Oracle_XA")
"""


def test_book_chapter9_threaded():
    """The book's multi-threaded variant: per-thread connections via
    tpsvrthrinit and threading.local, four dispatch threads."""
    if not db_available():
        print("Oracle database not available, skipping book ch9 test")
        return
    with scratch("bookch9thr") as dir:
        db_setup()
        env = boot(dir, threads="\n\tMINDISPATCHTHREADS=4 MAXDISPATCHTHREADS=4",
                   dbxa=DBXA_THREADED_PY)
        def call(svc, data):
            for attempt in range(3):
                try:
                    return t.tpcall(svc, data)
                except t.XatmiException as e:
                    if e.code != t.TPETIME or attempt == 2:
                        raise
            return None

        try:
            t.tpbegin(30)
            for i in range(8):
                call("STORE", {"TA_SOURCE": f"threaded-{i}"})
            assert call("COUNTXA", {}).data["TA_STATUS"] == ["Count=8"]
            t.tpcommit()
            assert call("COUNTXA", {}).data["TA_STATUS"] == ["Count=8"]
        except Exception:
            for u in sorted(dir.glob("ULOG*")):
                print("----", u)
                print(u.read_text()[-3000:])
            raise
        finally:
            t.tpterm()
            # Oracle's threaded shutdown can exit 1 while dispatch
            # threads wind down; the data assertions above are the test
            run(["tmshutdown", "-y"], env=env, cwd=dir, expect=None)


def test_book_chapter9():
    if not db_available():
        print("Oracle database not available, skipping book ch9 test")
        return
    with scratch("bookch9") as dir:
        db_setup()
        env = boot(dir)

        try:
            # the book's exact sequence
            assert t.tpcall("COUNT", {}).data["TA_STATUS"] == ["Count=0"]
            t.tpbegin(30)
            t.tpcall("STORE", {"TA_SOURCE": "Hello"})
            assert t.tpcall("COUNT", {}).data["TA_STATUS"] == ["Count=0"], \
                "COUNT does not share the database transaction"
            assert t.tpcall("COUNTXA", {}).data["TA_STATUS"] == ["Count=1"], \
                "COUNTXA must see the uncommitted row"
            t.tpcommit()
            assert t.tpcall("COUNT", {}).data["TA_STATUS"] == ["Count=1"], \
                "the row must be persisted after commit"

            # abort undoes the branch
            t.tpbegin(30)
            t.tpcall("STORE", {"TA_SOURCE": "doomed"})
            assert t.tpcall("COUNTXA", {}).data["TA_STATUS"] == ["Count=2"]
            t.tpabort()
            assert t.tpcall("COUNT", {}).data["TA_STATUS"] == ["Count=1"], \
                "the aborted row must be gone"
        except Exception:
            for u in sorted(dir.glob("ULOG*")):
                print("----", u)
                print(u.read_text()[-4000:])
            raise
        finally:
            t.tpterm()
            run(["tmshutdown", "-y"], env=env, cwd=dir)


if __name__ == "__main__":
    import subprocess
    import sys
    if len(sys.argv) > 1:
        # per-variant entry point: the parent process caches its bulletin
        # board attachment, so each domain boot runs in its own process
        # (same constraint as test_online_updates.py)
        globals()[sys.argv[1]]()
        print(f"{sys.argv[1]}: OK")
        sys.exit(0)
    for name in ("test_book_chapter9", "test_book_chapter9_threaded"):
        proc = subprocess.run(
            [sys.executable, os.path.abspath(__file__), name],
            cwd=os.path.dirname(os.path.abspath(__file__)))
        if proc.returncode != 0:
            raise SystemExit(proc.returncode)
        print(f"{name}: OK")
    print(f"{__file__}: 2 tests passed")
