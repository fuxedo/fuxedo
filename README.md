# Fuxedo

[![Build Status](https://travis-ci.org/fuxedo/fuxedo.svg?branch=master)](https://travis-ci.org/fuxedo/fuxedo)

## Why Fuxedo?

_"Fuxedo is a one-piece tuxedo that combines the elegance of a tuxedo and the convenience of a jumpsuit."_

Fuxedo is a toy project I started in late 2011 to play with C++11 and Boost libaries by building emulation of some Oracle Tuxedo APIs. Name of the project was just a word play about [Tuxedo](http://www.oracle.com/technetwork/middleware/tuxedo/overview/index.html), [BlackTie](http://narayana.jboss.org/subprojects/BlackTie) and "free"/"fun". Years later I have gained more experience and different bits and pieces about FML32, parsers, configuration files, shared memory, messages queues laying in my projects folder. I think it's time to put it all together.

## What is Fuxedo?

Fuxedo is an implementation of X/Open XATMI specification and some Oracle Tuxedo extensions of the API. The goal is to provide source code compatibility with core Tuxedo. Currently supported:

- Typed buffers API
  - STRING - C-style null-terminated strings.
  - CARRAY - binary blobs.
  - FML32 - self-describing fielded buffer like binary XML or JSON. Supports multiple levels of nested FML32 buffers.
- Boolean expressions of FML32 fielded buffers
- Tuxedo-specific APIs
  - tpimport, tpexport
- Programs
  - mkfldhdr32
  - ud32
  - tmloadcf
  - tmipcrm
  - buildserver
  - buildclient
- ...more to come

## More

If you need more information or help contact us at info@fuxedo.io
