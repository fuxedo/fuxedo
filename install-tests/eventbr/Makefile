ifndef TUXDIR
$(error TUXDIR is not set)
endif

export PATH:=$(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH:=$(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG:=$(CURDIR)/tuxconfig

check: client tuxconfig
	-rm -f ULOG.* hold.out
	-tmipcrm -y
	-tmboot -y
	sleep 3
	./client self
	tmshutdown -y
	tmboot -y
	sleep 3
	./client self
	tmshutdown -y
	grep -q ':TEST: SELFTEST OK' ULOG.*
	test `grep -c ':TEST: SELFTEST OK' ULOG.*` -eq 2
	grep -q 'TA_SUBSCRIPTION_HANDLE' tmusrevt.dat

ubbconfig: ubbconfig.in
	cat $< \
          | sed s:@TUXDIR@:$(TUXDIR):g \
          | sed s:@UNAME@:`uname -n`:g \
          | sed s:@CURDIR@:$(CURDIR):g > $@

tuxconfig: ubbconfig
	tmloadcf -y $<

client: client.c
	buildclient -o $@ -f $< -v -f "-g -Wl,--no-as-needed"

clean:
	-rm -f ubbconfig tuxconfig client ULOG.* stdout stderr hold.out hold.pid tmusrevt.dat
