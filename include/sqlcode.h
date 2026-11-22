// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// SQLCODE-style status constants for embedded-SQL applications. The
// diagnostics themselves ride the SQLCA. The QL* range is a legacy
// status-code space that classic embedded-SQL samples keep testing
// against even when the resource manager is a real database.

#ifndef FUXEDO_SQLCODE_H
#define FUXEDO_SQLCODE_H

#define SQL_OK 0
#define SQL_NOTFOUND 100

/* TUXEDO/SQL style status codes (the classic samples test these) */
#define QLRMERR -36
#define QLSORTERR -37
#define QLLINKERR -38
#define QLUNIXERR -39
#define QLINTOERR -40
#define QLPRECIS -41
#define QLUPKEY -42
#define QLDEADLOCK -43
#define QLFSERR -44
#define QLDDLERR -45
#define QLABORTED -46
#define QLBADNO -47
#define QLNOPREP -48
#define QLNOQUEST -49
#define QLINVEX -50
#define QLUPMAXREC -51
#define QLTEMPNAME -52
#define QLVARERR -53

#endif
