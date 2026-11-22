ifndef TUXDIR
$(error TUXDIR is not set)
endif

export PATH:=$(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH:=$(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG:=$(CURDIR)/tuxconfig
export QMCONFIG:=$(CURDIR)/queues

check: pump client tmsqm tuxconfig tlog
	-tmshutdown -y 2>/dev/null
	-sleep 2
	-mkdir -p queues/QS/IN queues/QS/OUT
	tmboot -y
	sleep 3
	./client
	./client concurrent
	tmshutdown -y
	@crlog tlog | head -8

ubbconfig: ubbconfig.in
	cat $< \
	  | sed s:@TUXDIR@:$(TUXDIR):g \
	  | sed s:@UNAME@:`uname -n`:g \
	  | sed s:@CURDIR@:$(CURDIR):g > $@

tuxconfig: ubbconfig
	tmloadcf -y $<

tlog:
	crdl -o 0 -s 100 tlog

pump: pump.c
	buildserver -o $@ -f $< -s PUMP -r TUXEDO/QM -v -f "-Wl,--no-as-needed"

client: client.c
	buildclient -o $@ -f $< -v -f "-Wl,--no-as-needed"

tmsqm:
	CC="$(CC)" buildtms -o TMSQM -r TUXEDO/QM -v

clean:
	-tmipcrm -y 2>/dev/null
	-tmshutdown -y 2>/dev/null
	-sleep 1
	-rm -rf ubbconfig tuxconfig tlog queues pump client TMSQM ULOG.* stdout stderr access.* core || true
