// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#pragma once
// Internal wire format between the tpenqueue/tpdequeue client shim and
// the TMQUEUE server. Fuxedo owns both ends; the field numbers live in
// the FML32 user range and are not part of any external contract.

#include <fml32.h>
#include <atmi.h>

namespace fux::tmq {

inline FLDID32 f_op() { return Fmkfldid32(FLD_LONG, 1); }
inline FLDID32 f_qname() { return Fmkfldid32(FLD_CARRAY, 2); }
inline FLDID32 f_ctl() { return Fmkfldid32(FLD_CARRAY, 3); }
inline FLDID32 f_data() { return Fmkfldid32(FLD_CARRAY, 4); }
inline FLDID32 f_flags() { return Fmkfldid32(FLD_LONG, 5); }

enum op { enqueue = 1, dequeue = 2 };

// Shared implementations behind tpenqueue/tpdequeue; the TMQUEUE server
// calls these directly for requests arriving over the wire.
int exec_enqueue(const char *qspace, const char *qname, TPQCTL *ctl,
                 const std::string &payload);
int exec_dequeue(const char *qspace, const char *qname, TPQCTL *ctl,
                 char **data, long *len, long flags);

}  // namespace fux::tmq
