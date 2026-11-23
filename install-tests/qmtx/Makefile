include ../mk/domain.mk

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
	-tmshutdown -y -k KILL
	@crlog tlog | head -8

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
