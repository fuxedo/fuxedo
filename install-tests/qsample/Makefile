include ../mk/domain.mk

check: qsvc client TMS_QM tuxconfig
	-rm -f ULOG.*
	-tmshutdown -y 2>/dev/null
	-tmipcrm -y 2>/dev/null
	-rm -rf QUE
	-sleep 2
	-mkdir -p QUE
	printf 'qspacecreate QSPACE 47695 100 6 4 9 3 errque n 16\nqopen QSPACE\nqcreate STRING fifo none 2 30 80%% 0%% -\nqcreate RPLYQ fifo none 2 30 80%% 0%% -\nqcreate NOFWD fifo none 2 30 80%% 0%% -\n' | QMCONFIG=$(CURDIR)/QUE qmadmin
	tmboot -y
	sleep 3
	./client
	tmshutdown -y
	-tmshutdown -y -k KILL
	grep -E -q '[!:]qsvc\..*: TEST: tpsvrinit called' ULOG.*
	grep -E -q '[!:]qsvc\..*: QSAMPLE: STRING got hello queue' ULOG.*

TMS_QM:
	buildtms -o $@ -r TUXEDO/QM -v

qsvc: server.c
	buildserver -o $@ -f $< -s STRING -v -f "-Wl,--no-as-needed"

client: client.c
	buildclient -o $@ -f $< -v -f "-Wl,--no-as-needed"

clean:
	-tmshutdown -y 2>/dev/null
	-tmipcrm -y 2>/dev/null
	-sleep 1
	-rm -rf ubbconfig tuxconfig qsvc client TMS_QM QUE ULOG.* stdout stderr access.* core || true
