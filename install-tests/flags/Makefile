include ../mk/domain.mk

check: server client tuxconfig
	-rm -f ULOG.*
	-tmipcrm -y
	-tmboot -y
	sleep 3
	./client
	tmshutdown -y
	-tmshutdown -y -k KILL
	grep -q ':TEST: FLAGMATRIX OK' ULOG.*
	grep -q ':TEST: PRIO call=1 prio_in_svc=77' ULOG.*
	grep -q ':TEST: PRIO call=2 prio_in_svc=50' ULOG.*
	grep -q ':TEST: PRIO call=3 prio_in_svc=60' ULOG.*

server: server.c
	buildserver -o $@ -f $< -s PRIO -v -f "-Wl,--no-as-needed"

client: client.c
	buildclient -o $@ -f $< -v -f "-Wl,--no-as-needed"

clean:
	-rm -f ubbconfig tuxconfig client server ULOG.* stdout stderr
