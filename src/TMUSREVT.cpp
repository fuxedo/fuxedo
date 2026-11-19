// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// TMUSREVT server: application event subscriptions. Shares its
// implementation with TMSYSEVT in evsrv.cpp; the binary name selects
// the database file (tmusrevt.dat).

#include <cstddef>

#include <atmi.h>

namespace fux {
namespace evsrv {
int init(int argc, char *argv[]);
void done();
}  // namespace evsrv
}  // namespace fux

extern "C" {
int _tmrunserver(int);
extern int _tmbuilt_with_thread_option;
}

int tpsvrinit(int argc, char *argv[]) { return fux::evsrv::init(argc, argv); }
void tpsvrdone() { fux::evsrv::done(); }

static struct tmdsptchtbl_t _tmdsptchtbl[] = {
    {NULL, NULL, NULL, 0, 0, NULL}};

static struct tmsvrargs_t _tmsvrargs = {
    NULL,           _tmdsptchtbl, 0,          tpsvrinit, tpsvrdone,
    _tmrunserver,   NULL,         NULL,       NULL,      NULL,
    NULL,           NULL,         NULL,       NULL};

struct tmsvrargs_t *_tmgetsvrargs() { return &_tmsvrargs; }

int main(int argc, char **argv) {
  _tmbuilt_with_thread_option = 0;
  return _tmstartserver(argc, argv, _tmgetsvrargs());
}
