# Buildable, runnable sources for the API reference pages.
#
#   make            build every example against TUXDIR
#   make check      boot the sample domain and run every example
#   make clean
#
# TUXDIR must point at a fuxedo install (e.g. tmp/dest/usr/local in a
# build tree, or an Oracle Tuxedo home: the sources are plain ATMI).
# Each page embeds one of these files; files that serve several pages
# carry // doc:region NAME markers which gen.py strips on render.

export TUXCONFIG := $(CURDIR)/tuxconfig
export PATH := $(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH := $(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export UNAME := $(shell uname -n)
export FLDTBLDIR32 := $(CURDIR)
export FIELDTBLS32 := demo
export TMPDIR := $(CURDIR)/.tmp

CLIENTS := tpcall acall cancel buffers session fmldemo txdemo

check: tuxconfig apisvr advsvr $(CLIENTS)
	mkdir -p $(TMPDIR)
	sh ./run.sh

ubbconfig: ubbconfig.in
	cat $< \
	  | sed "s:@TUXDIR@:$(TUXDIR):g" \
	  | sed "s:@UNAME@:$(UNAME):g" \
	  | sed "s:@CURDIR@:$(CURDIR):g" > $@

tuxconfig: ubbconfig
	tmloadcf -y $<

demo.h: demo
	mkfldhdr32 demo

apisvr: apisvr.c
	buildserver -o $@ -f $< -s TOUPPER -s FORWARD \
	  -v -f "-Wl,--no-as-needed"

advsvr: advsvr.c
	buildserver -o $@ -f $< -s GREET -v -f "-Wl,--no-as-needed"

fmldemo: fmldemo.c demo.h
	buildclient -o $@ -f $< -v -f "-I. -Wl,--no-as-needed"

$(filter-out fmldemo,$(CLIENTS)): %: %.c
	buildclient -o $@ -f $< -v -f "-Wl,--no-as-needed"

clean:
	-tmshutdown -y 2>/dev/null
	-tmipcrm -y 2>/dev/null
	-rm -f ubbconfig tuxconfig apisvr advsvr $(CLIENTS) demo.h \
	  demo.fml ULOG.* stdout stderr

.PHONY: check clean
