"""Book ch.6 flow: create TUXCONFIG from scratch via tpadmcall (pp. 82-83).

Requires a fuxedo installation: tpadmcall SET operates on the TUXCONFIG
file directly, no shared memory involved (offline mode, phase 1).
"""

import os

import tuxedo as t

from fuxtest import is_fuxedo, main, run, scratch

if not is_fuxedo():
    print("not a fuxedo installation, skipping tpadmcall tests")
    raise SystemExit(0)


def test_create_tuxconfig_from_scratch():
    with scratch("tpadmcall") as dir:
        tuxconfig = dir / "tuxconfig"
        os.environ["TUXCONFIG"] = str(tuxconfig)
        assert not tuxconfig.exists()

        t.tpadmcall({
            "TA_CLASS": "T_DOMAIN",
            "TA_OPERATION": "SET",
            "TA_STATE": "NEW",
            "TA_MASTER": "tuxapp",
            "TA_MODEL": "SHM",
            "TA_IPCKEY": 32769,
        })
        assert tuxconfig.exists()

        # T_DOMAIN creation implicitly creates the T_MACHINE record
        out = t.tpadmcall({"TA_CLASS": "T_MACHINE", "TA_OPERATION": "GET"}).data
        assert out["TA_LMID"][0] == "tuxapp"
        assert out["TA_TUXCONFIG"][0] == str(tuxconfig)

        t.tpadmcall({
            "TA_CLASS": "T_GROUP",
            "TA_OPERATION": "SET",
            "TA_STATE": "NEW",
            "TA_SRVGRP": "GROUP1",
            "TA_GRPNO": 1,
            "TA_LMID": "tuxapp",
        })

        t.tpadmcall({
            "TA_CLASS": "T_SERVER",
            "TA_OPERATION": "SET",
            "TA_STATE": "NEW",
            "TA_SRVGRP": "GROUP1",
            "TA_SERVERNAME": "ping.py",
            "TA_SRVID": 1,
            "TA_MIN": 2,
            "TA_MAX": 2,
            "TA_REPLYQ": "Y",
            "TA_MAXGEN": 2,
            "TA_RESTART": "Y",
            "TA_GRACE": 0,
            "TA_RQADDR": "ping",
        })

        proc = run(["tmunloadcf"], env={"TUXCONFIG": tuxconfig})
        assert "*RESOURCES" in proc.stdout
        assert "IPCKEY 32769" in proc.stdout
        assert "*MACHINES" in proc.stdout
        assert "*GROUPS" in proc.stdout
        assert "GROUP1 LMID=tuxapp GRPNO=1" in proc.stdout
        assert "*SERVERS" in proc.stdout
        assert "ping.py SRVGRP=GROUP1 SRVID=1" in proc.stdout
        assert "RQADDR=ping" in proc.stdout
        assert "REPLYQ=Y" in proc.stdout
        assert "MAXGEN=2" in proc.stdout
        assert "RESTART=Y" in proc.stdout


def test_get_and_getnext():
    with scratch("tpadmcall-get") as dir:
        tuxconfig = dir / "tuxconfig"
        os.environ["TUXCONFIG"] = str(tuxconfig)
        _populate()

        out = t.tpadmcall({
            "TA_CLASS": "T_DOMAIN", "TA_OPERATION": "GET"}).data
        assert out["TA_MASTER"][0] == "tuxapp"
        assert int(out["TA_IPCKEY"][0]) == 32769

        out = t.tpadmcall({
            "TA_CLASS": "T_GROUP", "TA_OPERATION": "GET"}).data
        assert out["TA_SRVGRP"] == ["GROUP1", "GROUP2"]

        out = t.tpadmcall({
            "TA_CLASS": "T_SERVER", "TA_OPERATION": "GETNEXT",
            "TA_CURSOR": "0"}).data
        assert out["TA_SERVERNAME"][0] == "one.py"
        out = t.tpadmcall({
            "TA_CLASS": "T_SERVER", "TA_OPERATION": "GETNEXT",
            "TA_CURSOR": out["TA_CURSOR"][0]}).data
        assert out["TA_SERVERNAME"][0] == "two.py"


def test_set_updates_and_invalid_deletes():
    with scratch("tpadmcall-set") as dir:
        tuxconfig = dir / "tuxconfig"
        os.environ["TUXCONFIG"] = str(tuxconfig)
        _populate()

        t.tpadmcall({
            "TA_CLASS": "T_SERVER", "TA_OPERATION": "SET",
            "TA_SRVGRP": "GROUP1", "TA_SRVID": 1, "TA_MIN": 5})
        out = t.tpadmcall({
            "TA_CLASS": "T_SERVER", "TA_OPERATION": "GET"}).data
        assert int(out["TA_MIN"][0]) == 5

        t.tpadmcall({
            "TA_CLASS": "T_GROUP", "TA_OPERATION": "SET",
            "TA_SRVGRP": "GROUP2", "TA_STATE": "INV"})
        out = t.tpadmcall({
            "TA_CLASS": "T_GROUP", "TA_OPERATION": "GET"}).data
        assert out["TA_SRVGRP"] == ["GROUP1"]


def _populate():
    t.tpadmcall({
        "TA_CLASS": "T_DOMAIN", "TA_OPERATION": "SET", "TA_STATE": "NEW",
        "TA_MASTER": "tuxapp", "TA_MODEL": "SHM", "TA_IPCKEY": 32769})
    t.tpadmcall({
        "TA_CLASS": "T_GROUP", "TA_OPERATION": "SET", "TA_STATE": "NEW",
        "TA_SRVGRP": "GROUP1", "TA_GRPNO": 1, "TA_LMID": "tuxapp"})
    t.tpadmcall({
        "TA_CLASS": "T_GROUP", "TA_OPERATION": "SET", "TA_STATE": "NEW",
        "TA_SRVGRP": "GROUP2", "TA_GRPNO": 2, "TA_LMID": "tuxapp"})
    t.tpadmcall({
        "TA_CLASS": "T_SERVER", "TA_OPERATION": "SET", "TA_STATE": "NEW",
        "TA_SRVGRP": "GROUP1", "TA_SERVERNAME": "one.py", "TA_SRVID": 1})
    t.tpadmcall({
        "TA_CLASS": "T_SERVER", "TA_OPERATION": "SET", "TA_STATE": "NEW",
        "TA_SRVGRP": "GROUP1", "TA_SERVERNAME": "two.py", "TA_SRVID": 2})


if __name__ == "__main__":
    main(globals())
