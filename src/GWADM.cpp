// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// GWADM server: the per-gateway-group admin endpoint, a fuxedo-provided
// binary booted once per gateway group:
//
//   GWADM SRVGRP=GW1 SRVID=1 CLOPT="--"
//
// v1 is an idle placeholder that keeps the group administrable: dmloadcf
// requires a GWADM entry in every gateway group and tmboot boots it like
// any server. Domain administration through it (the dmadmin subset)
// lands on top of this process later.

#include <cstddef>

#include <userlog.h>
#include <xatmi.h>

extern "C" {
int _tmrunserver(int);
extern int _tmbuilt_with_thread_option;
}

int tpsvrinit(int, char **) {
  return 0;
}

void tpsvrdone() {}

static struct tmdsptchtbl_t _tmdsptchtbl[] = {{NULL, NULL, NULL, 0, 0, NULL}};
static struct tmsvrargs_t tmsvrargs = {
    NULL, _tmdsptchtbl, 0,    tpsvrinit, tpsvrdone, _tmrunserver,
    NULL, NULL,         NULL, NULL,      NULL,      NULL,
    NULL, NULL};

struct tmsvrargs_t *_tmgetsvrargs() {
  return &tmsvrargs;
}

int main(int argc, char **argv) {
  _tmbuilt_with_thread_option = 0;
  return _tmstartserver(argc, argv, _tmgetsvrargs());
}
