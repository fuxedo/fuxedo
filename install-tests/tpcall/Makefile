include ../mk/domain.mk

check: server client tuxconfig
	-rm -f ULOG.*
	-tmboot -y
	sleep 3 # 22.1 returns before the bulletin board is ready
	./client
	tmshutdown -y
	-tmshutdown -y -k KILL
	grep -q ':TEST: SERVICE_TPSUCCESS called' ULOG.*
	grep -q ':TEST: SERVICE_TPFAIL called' ULOG.*

server: server.c
	buildserver -o $@ -f $< -s SERVICE_TPSUCCESS -s SERVICE_TPFAIL -s LENCHECK -s NULLSVC -s SWITCHSVC -v -f "-Wl,--no-as-needed"

client: client.c
	buildclient -o $@ -f $< -v -f "-Wl,--no-as-needed"

clean:
	-rm -f *.o ubbconfig tuxconfig client server ULOG.* stdout stderr access.*
