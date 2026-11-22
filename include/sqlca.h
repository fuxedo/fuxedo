// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// SQL Communications Area, the X/Open standard layout shared by every
// SQL embedding. The symbol `sqlca` is provided by the database client
// library at link time; embedded-SQL applications and the Pro*C
// runtime both reference this definition.

#ifndef FUXEDO_SQLCA_H
#define FUXEDO_SQLCA_H

#ifdef __cplusplus
extern "C" {
#endif

struct sqlca {
  char sqlcaid[8];
  long sqlcabc;
  long sqlcode;
  struct {
    unsigned short sqlerrml;
    char sqlerrmc[70];
  } sqlerrm;
  char sqlerrp[8];
  long sqlerrd[6];
  char sqlwarn[8];
  char sqlext[8];
};

extern struct sqlca sqlca;

#define SQLNOTFOUND 100

#define SQLCODE sqlca.sqlcode
#define SQLERRMC sqlca.sqlerrm.sqlerrmc
#define SQLERRML sqlca.sqlerrm.sqlerrml
#define SQLERRD sqlca.sqlerrd
#define SQLWARN sqlca.sqlwarn
#define SQLSTATE sqlca.sqlext

#ifdef __cplusplus
}
#endif

#endif
