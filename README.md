# Fuxedo

[![Build Status](https://travis-ci.org/fuxedo/fuxedo.svg?branch=master)](https://travis-ci.org/fuxedo/fuxedo)

## Why Fuxedo?

_"[Fuxedo is a one-piece tuxedo that combines the elegance of a tuxedo and the convenience of a jumpsuit.](https://www.youtube.com/watch?v=7seCxf8E1Ic)"_

Fuxedo is a toy project I started in late 2011 to play with C++11 and Boost libaries by building emulation of some Oracle Tuxedo APIs. Name of the project was just a word play about [Tuxedo](http://www.oracle.com/technetwork/middleware/tuxedo/overview/index.html), [BlackTie](http://narayana.jboss.org/subprojects/BlackTie) and "free"/"fun". Years later I have gained more experience and different bits and pieces about FML32, parsers, configuration files, shared memory, messages queues laying in my projects folder. I think it's time to put it all together.

## What is Fuxedo?

Fuxedo is an open source implementation of X/Open XATMI specification and some Oracle Tuxedo extensions of the API. The goal is to provide source code compatibility with core Tuxedo functionality: ideally a recompilation will be sufficient to port software from Tuxedo to Fuxedo. Fuxedo also keeps Tuxedo's configuration file formats and conventions silently ignoring the obscure parts.

There is no definition of what "core functionality" is but I will start with the functions I use on daily basis. Things like TSAM, Jolt, SALT, queues and adapters are out of scope.

Currently C++17 is used as implementation language mostly to learn what the new standard offers. This choice makes some of XATMI C-style interfaces weird to implement but life is too short to write everything in C.

Currently implemented:

- Typed buffers API
  - STRING - C-style null-terminated strings.
  - CARRAY - binary blobs.
  - FML32 - self-describing fielded buffer like binary XML or JSON. Supports multiple levels of nested FML32 buffers.
- Boolean expressions of FML32 fielded buffers
- Tuxedo-specific APIs
- Programs
  - mkfldhdr32
  - ud32
  - tmloadcf/tmunloadcf
  - tmipcrm
  - buildserver
  - buildclient
  - buildtms
- ...more to come

## Supported API function list

- Typed buffer functions
  - tpalloc
  - tprealloc
  - tpfree
  - tptypes
- Service routine functions
  - tpservice
  - tpreturn
- Advertising functions
  - tpadvertise
  - tpunadvertise
- Request/response service functions
  - tpcall
  - tpacall
  - tpcancel
  - tpgetrply
- Oracle Tuxedo XATMI extensions
  - tpimport/tpexport
  - tpinit/tpterm
- Oracle Tuxedo XATMI transaction interface
  - tpopen/tpclose
  - tpbegin/tpabort/tpcommit
  - tpgetlev
  - tpsuspend/tpresume
- X/Open TX interface
  - tx\_open/tx\_close
  - tx\_begin/tx\_rollback/tx\_commit
  - tx\_info
  - tx\_set\_commit\_return/tx\_set\_transaction\_control/tx\_set\_transaction\_timeout
- FML32
  - a lot
- Boolean expressions
  - Fboolco32
  - Fboolpr32
  - Fboolev32
  - Ffloatev32

## More

If you need more information or help contact us at info@fuxedo.io
