# Changelog

All notable changes to Fuxedo are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1.0.5] - 2026-11-22

- `ud`/`ud32` input protocol pinned on Oracle 12.2.2: a blank line sends
  the current buffer (which then holds the reply), a line with `n`
  starts a new buffer, and input pending at EOF is dropped. `ud32` joins
  the application at startup like the platform, so the machine ENVFILE
  is in effect before the first buffer parses.
- The machine ENVFILE now applies to joining clients at tpinit, not only
  to booted servers (pinned on Oracle 12.2.2 with strace).
- Data-dependent routing honors `BUFTYPE="FML"`: 16-bit criteria resolve
  against the 16-bit field tables and read in the buffer's own flavor.
- Failed AUTOTRAN services report their own rval (`TPESVCFAIL`) to the
  caller again; the server-side rollback no longer surfaces as
  `TPEABORT` (only a failed commit drive does).
- `tpinit`/`tpterm` return `-1` when the client cannot attach or
  detach, instead of swallowing the failure behind a success code.
- The cached configuration and bulletin board are dropped at `tpterm`
  and reloaded when `TUXCONFIG` changes, so one process can join
  another application (or the same key after a reload) cleanly.
- `TMQFORWARD` forwards transactionally: dequeue and target service
  commit or abort together, a failed forward aborts so the message is
  redelivered after the queue's retry delay, and messages move to the
  failure queue after the retries limit. Worker threads of a server
  process (TMQFORWARD's forwarders) can begin transactions without a
  per-thread `tx_open`.
- `install-tests/orarm` now runs the two-RM pipeline end to end on a
  live Oracle: an AUTOTRAN service inserts a row and enqueues its id
  in one transaction (Oracle XA + /Q), and `TMQFORWARD` feeds a reader
  service that processes the row with redelivery when the row is not
  visible yet.
- Transaction timeouts are enforced the platform way (verified on
  Oracle 12.2.2): a transaction past its `tpbegin` timeout or service
  `TRANTIME` answers `TPETIME` to calls and `tpcommit` rolls it back.
- `fux.env` installs at the prefix root, the shape of the platform's
  `tux.env`: one source sets `TUXDIR`, `PATH`, and the loader paths
  for shells. A preset `TUXDIR` wins so a DESTDIR tree works too.
- `install-tests/mk/domain.mk` carries the toolchain exports and the
  `ubbconfig` template expansion every domain test repeated; the
  system servers share the `tmsvrargs` initializer through
  `fuxedo_server_args` in `src/servermain.h` instead of copy-pasting
  the positional 14-field struct.

## [1.0.4] - 2026-11-22

### Added

- FML16 and VIEW16: the classic 16-bit fielded-buffer flavor (Fadd,
  Fchg, Fdel, Fget, Fvall, F16to32/F32to16 converters, viewc-compiled
  views) with the exact Oracle-pinned return contract, plus the 16-bit
  tools `mkfldhdr` and `ud`.
- `X_OCTET`, `X_COMMON`, and `VIEW32` as typed-buffer names with the
  platform's size rules (1024-byte minimum for view-backed buffers,
  `TPEINVAL` for zero-sized `CARRAY`/`X_OCTET`/`XML`).
- `tpalloc("VIEW", name, 0)` resolves the view definition's size;
  unknown view names and views larger than the request fail with
  `TPEOS`, pinned on Oracle 12.2.2.
- `buildclient -r rmname` resolves the resource manager's link line
  from the RM file, like the platform's buildclient.
- Cross-domain two-phase commit through GWTDOMAIN with persistent
  remote branches, and client-side transactions over WSL.
- USDT probes (provider `fuxedo`) at the ATMI, transaction, XA, /Q and
  domain-wire boundaries, plus uprobe-friendly builds.
- Event broker: `tpsubscribe`/`tpunsubscribe` with FML filters,
  `tppost`, `tpnotify`, `tpbroadcast`, `TMUSREVT`/`TMSYSEVT`, and an
  Oracle-compatible on-disk subscription database.
- Data-dependent routing across groups, including boot-order
  fallthrough for staged rollouts (`test_rollout`).
- Runtime admin: `adv`/`unadv`/`suspend`/`resume`/`chgserver` with
  audit logging, read-only attach, `printtrans`, `crdl`/`crlog`.
- POSIX-style `getopt(3)` interposition (exported from libfuxedo) and
  full-argv `tpsvrinit`, pinned against Oracle 12.2.2 - classic
  CLOPT parsing (`-A -- -T 100`) works unchanged.
- API reference (docs/api) with tested, compilable examples for the
  XATMI and FML32 surface, return-value semantics, and flag tables.

### Changed

- FML success returns pinned to the platform: the whole family answers
  `1` (16-bit and 32-bit), `tpexport`/`tpimport` answer `1`, and
  `tptypes` returns the buffer's allocated size. `Fnext` keeps its
  1/0/-1 walk protocol.
- `tpalloc` size semantics pinned: per-type defaults (STRING 512,
  FML/FML32/VIEW 1024), `CARRAY`/`X_OCTET`/`XML` require an explicit
  size, view buffers must fit.
- All XA calls use rmid 0 per process, the platform convention.
- `tmshutdown` waits unbounded for the polite shutdown, pinned on
  Oracle 12.2.2 (a 25s `tpsvrdone` freeze kept it waiting 28s).
- The bankapp-for-Oracle-database sample runs end to end: builds with
  `buildserver` + real proc precompilation, boots 26 servers with
  per-group TMS, and its AUTOTRAN database effects persist in Oracle
  through the full two-phase path (verified by a repeatable harness,
  same expectations cross-checked against Oracle Tuxedo 12.2.2).

### Fixed

- AUTOTRAN transactions were never committed: the begin path cleared
  the own-begin flag that the tpreturn commit drive gated on.
- The commit drive read the XID after `tx_end` had wiped the thread
  context, so the prepare phase never found the transaction.
- Transaction-table leak: completed transactions never freed their
  shared-table entry, so the Nth begin aborted the server.
- Duplicate `tpadvertise` of an identical (service, queue, server)
  threw and turned any earlier crash into a permanent restart loop;
  now idempotent like the platform.
- Shared-queue admin deadlock: a `.stop` requeued for another MSSQ
  member used a message type outside every receive window, hanging
  `tmshutdown` forever on a TMS pair.
- TMS recovery scanned for in-doubt transactions with an rmid it never
  opened (`XAER_PROTO` on every boot).
- `Fget`/`Fget32` accept a NULL length pointer for STRING fields
  (pinned on 12.2.2); classic code such as bankapp relies on it.
- `tprealloc` updated the allocation but not the reported size.
- `tpimport` wire sniffing read past its scratch buffer after the
  type-name field widened.
- `tpcall` of an in-transaction service that switched buffer types,
  empty-string replies, and exact `TPSVCINFO.len` semantics pinned and
  covered.

## [1.0.3] - 2026-11-21

- Docs: API reference pages for the remaining XATMI surface.

## [1.0.2] - 2026-11-21

- Cross-domain 2PC probes, staged-rollout demo, more compatibility
  suites.

## [1.0.1] - 2026-11-21

- Repository and docs cleanup.

## [1.0.0] - 2026-11-21

- First stable line. Oracle Tuxedo sample applications build and run
  with fuxedo toolchains; all book examples work.

## [0.9.3] - 2026-11-20

- Documentation set: status, migration, FAQ, quickstart.

## [0.9.2] - 2026-11-19

- Domains (GWTDOMAIN) with dmadmin, encrypted links, DMTLOG.
- /Q folder-based queue spaces with qmadmin parity.
- Event broker groundwork, probes, hardening round.

## [0.9.1] - 2026-11-18

- First public patch release: ATMI core, FML32/VIEW32, transactions
  with a folder TLOG, tmadmin/tmloadcf/tmboot toolchain.
