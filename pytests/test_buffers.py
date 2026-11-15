"""Buffer-level tests through the tuxedo-python module.

Vendor neutral: runs against both fuxedo and Oracle Tuxedo installs.

Needs FLDTBLDIR32/FIELDTBLS32 pointing at a tpadm field table (the
pytests/Makefile sets this up).
"""

import os

import tuxedo as t

from fuxtest import main


def test_export_import_roundtrip():
    buf = {"TA_CLASS": ["T_SVCGRP"], "TA_OPERATION": ["GET"]}
    assert t.tpimport(t.tpexport(buf)) == buf
    assert t.tpimport(t.tpexport(buf, t.TPEX_STRING), t.TPEX_STRING) == buf


def test_field_id_functions():
    assert t.Fname32(t.Fldid32("TA_OPERATION")) == "TA_OPERATION"
    assert t.Fldtype32(t.Fmkfldid32(t.FLD_STRING, 10)) == t.FLD_STRING
    assert t.Fldno32(t.Fmkfldid32(t.FLD_STRING, 10)) == 10


def test_oracle_compatible_field_ids():
    # Field id encoding and numbers aligned with Oracle headers; the same
    # values come out of an Oracle installation.
    assert t.Fldid32("TA_CLASS") == 167778162
    assert t.Fldid32("SRVCNM") == 167772168


def test_binary_payload():
    binstr = b"\xc1 hello"
    buf = {"TA_OPERATION": binstr}
    out = t.tpimport(t.tpexport(buf))["TA_OPERATION"][0]
    assert out.encode(errors="surrogateescape") == binstr
    again = t.tpimport(t.tpexport({"TA_OPERATION": out}))["TA_OPERATION"][0]
    assert again.encode(errors="surrogateescape") == binstr


def test_utf8_payload():
    data = "glāžšķūņrūķītis"
    buf = {"TA_OPERATION": data}
    assert t.tpimport(t.tpexport(buf))["TA_OPERATION"][0] == data
    raw = t.tpimport(t.tpexport(buf))["TA_OPERATION"][0].encode("utf8")
    assert raw == data.encode("utf8")


def test_error_exception_carries_code():
    try:
        t.tpimport(b"not an exported buffer")
        raise AssertionError("tpimport should have failed")
    except t.XatmiException as e:
        assert e.code != 0


if __name__ == "__main__":
    main(globals())
