# Fuxedo

Making the world a better place through minimal message-oriented transport layers.


[![Build Status](https://github.com/fuxedo/fuxedo/actions/workflows/fuxedo.yml/badge.svg)](https://github.com/fuxedo/fuxedo/actions/workflows/fuxedo.yml)
[![codecov](https://codecov.io/gh/fuxedo/fuxedo/branch/master/graph/badge.svg)](https://codecov.io/gh/fuxedo/fuxedo)
[![Coverage Status](https://coveralls.io/repos/github/fuxedo/fuxedo/badge.svg)](https://coveralls.io/github/fuxedo/fuxedo)

## Why Fuxedo?

_"[Fuxedo is a one-piece tuxedo that combines the elegance of a tuxedo and the convenience of a jumpsuit.](https://www.youtube.com/watch?v=7seCxf8E1Ic)"_

Fuxedo started in late 2011 as a toy project: an excuse to play with C++11 and the Boost libraries by building an emulation of some Oracle Tuxedo APIs. The name is a word play on [Tuxedo](http://www.oracle.com/technetwork/middleware/tuxedo/overview/index.html) and "free"/"fun". Years later, with more experience and bits and pieces about FML32, parsers, configuration files, shared memory and message queues gathered along the way, what started as a toy is now being made solid: an MIT-licensed implementation built to run real Tuxedo applications.

[It is also free to use, modify and distribute for commercial and non-commercial purposes.](https://en.wikipedia.org/wiki/MIT_License)

## What is Fuxedo?

Fuxedo is an open source implementation of X/Open XATMI specification and some Oracle Tuxedo extensions of the API. The goal is to provide source code compatibility with core Tuxedo functionality: ideally a recompilation will be sufficient to port software from Tuxedo to Fuxedo. Fuxedo also keeps Tuxedo's configuration file formats and conventions silently ignoring the obscure parts.

There is no definition of what "core functionality" is but I will start with the functions I use on daily basis. Things like TSAM, Jolt, SALT, queues and adapters are out of scope.

Currently C++17 is used as implementation language mostly to learn what the new standard offers. This choice makes some of XATMI C-style interfaces weird to implement but life is too short to write everything in C.

Currently implemented:

- Typed buffers API
  - STRING - C-style null-terminated strings.
  - CARRAY - binary blobs.
  - FML32 - self-describing fielded buffer like binary XML or JSON.
    Supports multiple levels of nested FML32 buffers, and the boolean
    expression compiler (Fboolco32 trees evaluate on both sides of a
    wire).
  - VIEW32 - C structure views compiled by viewc32 from view files.
  - XML - the tpxmltofml32/tpfml32toxml conversions and the XML typed
    buffer, so XML-based services from the Tuxedo samples work.
- Request/response with MSSQ server sets, including secondary queues and
  multithreaded dispatch.
- Distributed transactions: tpbegin/tpcommit/tpabort with two-phase
  commit, a folder-based transaction log (crdl/crlog), recovery after a
  crash, and transactional /Q.
- /Q queues: tpenqueue/tpdequeue, TMQUEUE and TMQFORWARD servers, a
  qmadmin that speaks the same command language, queue spaces stored as
  directories and messages as files.
- EventBroker: tpsubscribe/tpunsubscribe/tppost/tpnotify/tpsetunsol/
  tpchkunsol, with the TMUSREVT/TMSYSEVT servers and the same on-disk
  subscription database format.
- Domains and workstations: dmloadcf/dmunloadcf, GWTDOMAIN gateways with
  encrypted links, dmadmin, the WSL listener, and libwsc-style WSNADDR
  clients, including transactions over the workstation link.
- Administration: tpadmcall against the configuration and the bulletin
  board, tmadmin with the runtime service controls, per-command audit
  lines, and offline utilities.
- Tracing built in: USDT probes (provider "fuxedo") when built with
  systemtap-sdt-dev, uprobe-friendly binaries otherwise, plus the
  changetrace-style ULOG dye trail.
- Programs
  - mkfldhdr32
  - viewc32, viewdis32, ud32
  - tmloadcf/tmunloadcf, tmipcrm
  - tmboot/tmshutdown, tmadmin
  - qmadmin, crdl, crlog
  - dmloadcf/dmunloadcf, dmadmin
  - buildserver, buildclient, buildtms
  - BBL, TMS, TMQUEUE, TMQFORWARD, TMUSREVT, TMSYSEVT, WSL, GWTDOMAIN
- ...more to come

## Supported API function list

- Typed buffer functions
  - tpalloc
  - tprealloc
  - tpfree
  - tptypes
  - tpimport/tpexport
- Service routine functions
  - tpservice
  - tpreturn
  - tpforward
- Advertising functions
  - tpadvertise
  - tpadvertisex
  - tpunadvertise
- Request/response service functions
  - tpcall
  - tpacall
  - tpcancel
  - tpgetrply
  - tpsblktime/tpgblktime
- Client functions
  - tpinit/tpterm
  - tpappthrinit/tpappthrterm
- Oracle Tuxedo XATMI transaction interface
  - tpopen/tpclose
  - tpbegin/tpabort/tpcommit
  - tpgetlev
  - tpsuspend/tpresume
- EventBroker functions
  - tpsubscribe/tpunsubscribe
  - tppost
  - tpnotify
  - tpbroadcast
  - tpsetunsol/tpchkunsol
- /Q functions
  - tpenqueue/tpdequeue
- Administration
  - tpadmcall
- Buffer conversion
  - tpxmltofml32/tpfml32toxml
- Context functions
  - tpgetctxt/tpsetctxt (single context, like Tuxedo without TPMULTICONTEXTS)
- X/Open TX interface
  - tx\_open/tx\_close
  - tx\_begin/tx\_rollback/tx\_commit
  - tx\_info
  - tx\_set\_commit\_return/tx\_set\_transaction_control/tx\_set\_transaction_timeout
- FML32
  - a lot
- VIEW32
  - viewc32-compiled accessors, Fv*32 calls, FML32/VIEW32 interop
- Boolean expressions
  - Fboolco32
  - Fboolpr32
  - Fboolev32
  - Ffloatev32

## Testing layout

- `tests/compat/` - C-API compatibility suite (FML32, expressions, XATMI
  buffers, base64, invalid-argument hardening). Vendor neutral: also
  compiled and run against Oracle Tuxedo via `install-tests/unit` with
  `TUXDIR` pointing at either implementation.
- `tests/impl/` - fuxedo implementation tests (MIB, IPC, /Q, transactions,
  config). fuxedo only.
- `install-tests/` - CLI and end-to-end compatibility: buildclient,
  buildserver, tmboot/tmshutdown, tpcall family, tpforward, EventBroker,
  /Q, restarts. Run against whichever `TUXDIR` points to; Oracle Tuxedo
  12.2.2 runs in continuous integration.
- `pytests/` - Python-driven CLI compatibility tests via tuxedo-python,
  including the book's `tpadmcall` flow, online updates, domains,
  workstation clients, and crash recovery.

## Compatibility with Oracle Tuxedo

Fuxedo tries to be compatible with Oracle Tuxedo for all functionality implemented so far. That is ensured by executing the same tests cases against both Fuxedo and Oracle Tuxedo:

- Running `make` from `install-tests` folder will execute all "intergration tests" with whatever `TUXDIR` points to. That is done for every change in Fuxedo and time-to-time with Oracle Tuxedo 12.2
- Part of unittests (FML32, Boolean expressions) are compiled and executed as "integration tests" under `install-tests/unit`

Some of the tests fail with Oracle Tuxedo and I have reported several issues to Oracle, some are fixed but require additional patches from Oracle to be installed.

## More

Check out some other projects:

- https://github.com/fuxedo/tuxtrace - ULOG-based tracing of Tuxedo application
- https://github.com/fuxedo/tuxmon - A top-like monitoring tool
- https://github.com/aivarsk/tuxedo-python - Python3 bindings for Oracle Tuxedo

I'm interested to hear what features of Oracle Tuxedo your project is using and what should be implemented in Fuxedo. If you can share the code or let me take a look under NDA - even better.

For bugs and feature requests, [open an issue on GitHub](https://github.com/fuxedo/fuxedo/issues/new). For anything else, write to [hello@fuxedo.io](mailto:hello@fuxedo.io).
