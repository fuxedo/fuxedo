include ../mk/domain.mk

check: server client tuxconfig
	-rm -f ULOG.*
	-tmipcrm -y
	-tmboot -y
	sleep 3 # 22.1 returns before the bulletin board is ready
	timeout 120 ./client
	# caught signal mid-wait -> TPGOTSIG (needs the signal while the
	# client blocks in the 10s SLEEP call)
	( ./client gotsig > gotsig.out 2>&1 & gp=$$!; sleep 2; kill -TERM $$gp; wait $$gp )
	grep -q 'gotsig: tpcall rc=-1 tperrno=15' gotsig.out
	tmshutdown -y
	-tmshutdown -y -k KILL
	grep -q ':TEST: SLEEP called' ULOG.*
	grep -q ':TEST: SERVICE called' ULOG.*

server: server.c
	buildserver -o $@ -f $< -s SERVICE -s SLEEP -v -f "-g -Wl,--no-as-needed"

client: client.c
	buildclient -o $@ -f $< -v -f "-g -Wl,--no-as-needed"

clean:
	-rm -f *.o ubbconfig tuxconfig client server ULOG.* stdout stderr access.* gotsig.out*
