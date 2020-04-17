ifndef TUXDIR
$(error TUXDIR is not set)
endif
ifndef ORACLE_HOME
$(error ORACLE_HOME is not set)
endif

export PATH:=$(TUXDIR)/bin:$(ORACLE_HOME)/bin:$(PATH)
export LD_LIBRARY_PATH:=$(TUXDIR)/lib:$(ORACLE_HOME)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG:=$(CURDIR)/tuxconfig
export TNS_ADMIN:=$(CURDIR)

check: oraxa server server1 server2 client tuxconfig
	-rm -f stderr stdout ULOG.*
	-tmipcrm -y
	set | grep TNS
	sqlplus fuxedo/fuxedo@orcl @pre.sql
	tmboot -y
	./client
	./client
	tmshutdown -y
	sqlplus fuxedo/fuxedo@orcl @post.sql >> `echo ULOG.*`
	grep -q ':TEST: SERVICE_COMMIT called' ULOG.*
	grep -q ':TEST: SERVICE_ABORT called' ULOG.*
	grep -q ':TEST: SERVICE1 called' ULOG.*
	grep -q ':TEST: SERVICE2 called' ULOG.*
	grep -q 'MAX=6' ULOG.*


ubbconfig: ubbconfig.in
	cat $< \
          | sed s:@TUXDIR@:$(TUXDIR):g \
          | sed s:@UNAME@:`uname -n`:g \
          | sed s:@CURDIR@:$(CURDIR):g > $@

server1.c: server1.pc
	proc INAME=$< PARSE=NONE LINES=YES

server2.c: server2.pc
	proc INAME=$< PARSE=NONE LINES=YES

tuxconfig: ubbconfig
	tmloadcf -y $<

oraxa:
	buildtms -v -o $@ -r Oracle_XA

server: server.c
	buildserver -o $@ -r Oracle_XA -f $< -s SERVICE_COMMIT -s SERVICE_ABORT -v -f "-Wl,--no-as-needed"

server1: server1.c
	buildserver -o $@ -r Oracle_XA -f $< -s SERVICE1 -v -f "-I$(ORACLE_HOME)/precomp/public -Wl,--no-as-needed"

server2: server2.c
	buildserver -o $@ -r Oracle_XA -f $< -s SERVICE2 -v -f "-I$(ORACLE_HOME)/precomp/public -Wl,--no-as-needed"

client: client.c
	buildclient -o $@ -f $< -v -f "-Wl,--no-as-needed"

clean:
	-tmipcrm -y
	killall server server1 server2 client BBL oraxa
	-rm -f *.o ubbconfig tuxconfig client server server1 server2 oraxa ULOG.* stdout stderr access.* server1.c server2.c *.lis
