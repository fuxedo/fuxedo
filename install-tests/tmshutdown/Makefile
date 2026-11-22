ifndef TUXDIR
$(error TUXDIR is not set)
endif

export PATH:=$(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH:=$(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG:=$(CURDIR)/tuxconfig

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
 
ubbconfig: ubbconfig.in
	cat $< \
          | sed s:@TUXDIR@:$(TUXDIR):g \
          | sed s:@UNAME@:`uname -n`:g \
          | sed s:@CURDIR@:$(CURDIR):g > $@

tuxconfig: ubbconfig
	tmloadcf -y $<

server: server.c
	buildserver -o $@ -f $< -s SERVICE -v -f "-Wl,--no-as-needed"

clean:
	-rm -f *.o ubbconfig tuxconfig server ULOG.* stdout stderr access.*
