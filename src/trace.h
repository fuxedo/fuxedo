// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#pragma once
// Dye tracing: one line per observable API event in the platform's
// "{ name(args)" / "} name = result" shape, enabled per server through
// changetrace (bulletin board) and per client through the TMTRACE
// environment. When disabled the cost is a single branch; there is
// deliberately no per-function instrumentation, only the API surface
// that matters for matching behavior across implementations.

namespace fux::trace {

// Whether tracing is currently on for this thread.
bool enabled();

// Set the thread flag (server dispatch derives it from the server's
// trace level; the initial value comes from TMTRACE).
void enable(bool on);

// Emit one "TRACE:at: ..." line. No-op when disabled.
void event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace fux::trace
