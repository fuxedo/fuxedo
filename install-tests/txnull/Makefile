include ../mk/domain.mk

check: server client tuxconfig
	-rm -f ULOG.*
	-tmipcrm -y
	-tmboot -y
	sleep 3 # 22.1 returns before the bulletin board is ready
	timeout 120 ./client
	-tmshutdown -y
	-tmshutdown -y -k KILL
	grep -q ':TEST: SERVICE_INPUTS called' ULOG.*
	grep -q ':TEST: SERVICE_COMMIT called' ULOG.*
	grep -q ':TEST: SERVICE_ABORT called' ULOG.*
	grep -q ':TEST: SERVICE_SUSPEND called' ULOG.*
	grep -q ':TEST: SERVICE_TX called' ULOG.*
	grep -q ':TEST: SERVICE_INV called' ULOG.*

server: server.c
	buildserver -o $@ -f $< -s SERVICE_INV -s SERVICE_TX -s SERVICE_INPUTS -s SERVICE_COMMIT -s SERVICE_ABORT -s SERVICE_SUSPEND -v -f "-Wl,--no-as-needed"

client: client.c
	buildclient -o $@ -f $< -v -f "-Wl,--no-as-needed"

clean:
	-rm -f *.o ubbconfig tuxconfig client server ULOG.* stdout stderr access.*
