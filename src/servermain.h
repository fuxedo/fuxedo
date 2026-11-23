#pragma once
// This file is part of Fuxedo
// Copyright (C) Aivars Kalvans <aivars.kalvans@gmail.com>

// Shared scaffolding for the fuxedo system servers: the positional
// 14-field tmsvrargs initializer lives here, type-checked, instead of
// being copy-pasted into every server main. Servers either run the
// platform thread hooks (BBL, TMS, the queue servers) or pass a NULL
// tail (the gateway and event servers).

#include <atmi.h>

extern "C" {
int _tmrunserver(int);
extern int _tmbuilt_with_thread_option;
}

inline tmsvrargs_t fuxedo_server_args(xa_switch_t *xa_switch,
                                      tmdsptchtbl_t *dispatch, bool threads) {
  return tmsvrargs_t{xa_switch,
                     dispatch,
                     0,
                     tpsvrinit,
                     tpsvrdone,
                     _tmrunserver,
                     nullptr,
                     nullptr,
                     nullptr,
                     nullptr,
                     threads ? tprminit : nullptr,
                     threads ? tpsvrthrinit : nullptr,
                     threads ? tpsvrthrdone : nullptr,
                     nullptr};
}
