ifndef TUXDIR
$(error TUXDIR is not set)
endif

export PATH:=$(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH:=$(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG:=$(CURDIR)/tuxconfig

# a conversational service blocks in tprecv; fuxedo serves it from a
# dispatch thread pool, which must be configured. Oracle runs
# conversational services on their own thread and wants a
# single-context server here.
ifneq ($(wildcard $(TUXDIR)/lib/libfuxedo.so),)
THREADLINE = MINDISPATCHTHREADS=2 MAXDISPATCHTHREADS=2 
else
THREADLINE =
endif

check: server client tuxconfig
	-rm -f ULOG.*
	-tmipcrm -y
	-tmboot -y
	sleep 3 # 22.1 returns before the bulletin board is ready
	timeout 120 ./client basic
	timeout 120 ./client fail
	timeout 120 ./client cdiscon
	timeout 120 ./client srvfirst
	timeout 120 ./client negative
	timeout 120 ./client large
	tmshutdown -y
	-tmshutdown -y -k KILL
	grep -q ':TEST: BASIC OK' ULOG.*
	grep -q ':TEST: FAIL OK' ULOG.*
	grep -q ':TEST: CDISCON OK' ULOG.*
	grep -q ':TEST: SRVFIRST OK' ULOG.*
	grep -q ':TEST: NEGATIVE OK' ULOG.*
	grep -q ':TEST: LARGE OK' ULOG.*
	grep -q ':TEST: ECHO recv len=7 revent=0x20 data=hello2' ULOG.*
	grep -qE ':TEST: SLEEP (second recv rc=0 revent=0x1|disconnect in first recv rc=0 revent=0x1)' ULOG.*

ubbconfig: ubbconfig.in
	cat $< \
          | sed s:@TUXDIR@:$(TUXDIR):g \
          | sed s:@UNAME@:`uname -n`:g \
          | sed s:@CURDIR@:$(CURDIR):g \
          | sed 's|@THREADLINE@|$(THREADLINE)|' > $@

tuxconfig: ubbconfig
	tmloadcf -y $<

server: server.c
	buildserver -o $@ -f $< -s ECHO -s FAILSVC -s SLEEP -s SENDFIRST -s ECHOBIG -v -f "-Wl,--no-as-needed"

client: client.c
	buildclient -o $@ -f $< -v -f "-Wl,--no-as-needed"

clean:
	-rm -f *.o ubbconfig tuxconfig client server ULOG.* stdout stderr access.*
