include ../mk/domain.mk

check: server tuxconfig
	-rm -f ULOG.*
	-tmipcrm -y
	-tmboot -y
	sleep 3 # 22.1 returns before the bulletin board is ready
	tmshutdown -i 3
	sleep 1
	grep -qE '[!:]server\..*:TEST: tpsvrdone called.*-i 3' ULOG.*
	grep -qE '[!:]server\..*:TEST: tpsvrdone done.*-i 3' ULOG.*
	tmshutdown -i 2
	sleep 1
	grep -qE '[!:]server\..*:TEST: tpsvrdone called.*-i 2' ULOG.*
	grep -qE '[!:]server\..*:TEST: tpsvrdone done.*-i 2' ULOG.*
	tmshutdown -i 1
	sleep 1
	grep -qE '[!:]server\..*:TEST: tpsvrdone called.*-i 1' ULOG.*
	grep -qE '[!:]server\..*:TEST: tpsvrdone done.*-i 1' ULOG.*
	tmshutdown -y
	-tmshutdown -y -k KILL
 

server: server.c
	buildserver -o $@ -f $< -s SERVICE -v -f "-Wl,--no-as-needed"

clean:
	-rm -f *.o ubbconfig tuxconfig server ULOG.* stdout stderr access.*
