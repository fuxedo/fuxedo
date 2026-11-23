include ../mk/domain.mk

check: server client tuxconfig
	-rm -f ULOG.*
	-tmboot -y
	sleep 3 # 22.1 returns before the bulletin board is ready
	./client
	tmshutdown -y
	-tmshutdown -y -k KILL
	grep -q ':TEST: SERVICE called' ULOG.*

server: server.c
	buildserver -o $@ -f $< -s SERVICE -v -f "-Wl,--no-as-needed"

client: client.c
	buildclient -o $@ -f $< -v -f "-Wl,--no-as-needed"

clean:
	-rm -f *.o ubbconfig tuxconfig client server ULOG.* stdout stderr access.*
