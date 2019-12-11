ifndef TUXDIR
$(error TUXDIR is not set)
endif

export PATH:=$(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH:=$(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG:=$(CURDIR)/tuxconfig

check: server client tuxconfig
	-rm -f ULOG.*
	-tmipcrm -y
	tmboot -y
	./client
	tmshutdown -y
	grep -q ':TEST: SERVICE_INPUTS called' ULOG.*
	grep -q ':TEST: SERVICE_COMMIT called' ULOG.*
	grep -q ':TEST: SERVICE_ABORT called' ULOG.*
	grep -q ':TEST: SERVICE_SUSPEND called' ULOG.*
	grep -q ':TEST: SERVICE_TX called' ULOG.*

ubbconfig: ubbconfig.in
	cat $< \
          | sed s:@TUXDIR@:$(TUXDIR):g \
          | sed s:@UNAME@:`uname -n`:g \
          | sed s:@CURDIR@:$(CURDIR):g > $@

tuxconfig: ubbconfig
	tmloadcf -y $<

server: server.c
	buildserver -o $@ -f $< -s SERVICE_TX -s SERVICE_INPUTS -s SERVICE_COMMIT -s SERVICE_ABORT -s SERVICE_SUSPEND -v -f "-Wl,--no-as-needed"

client: client.c
	buildclient -o $@ -f $< -v -f "-Wl,--no-as-needed"

clean:
	-rm -f *.o ubbconfig tuxconfig client server ULOG.* stdout stderr access.*
