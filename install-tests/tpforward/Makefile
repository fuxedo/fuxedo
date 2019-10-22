ifndef TUXDIR
$(error TUXDIR is not set)
endif

export PATH:=$(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH:=$(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG:=$(CURDIR)/tuxconfig

check: servera serverb client tuxconfig
	-rm -f ULOG.*
	tmboot -y
	./client
	tmshutdown -y
	grep -q ':TEST: SERVICEA called' ULOG.*
	grep -q ':TEST: SERVICEB called' ULOG.*
	grep -q ':TEST: FAILSERVICE called' ULOG.*
	grep -q ':TEST: LOCALSERVICE called' ULOG.*

ubbconfig: ubbconfig.in
	cat $< \
          | sed s:@TUXDIR@:$(TUXDIR):g \
          | sed s:@UNAME@:`uname -n`:g \
          | sed s:@CURDIR@:$(CURDIR):g > $@

tuxconfig: ubbconfig
	tmloadcf -y $<

servera: servera.c
	buildserver -o $@ -f $< -s SERVICEA -s FAILSERVICE -s LOCALSERVICE -v -f "-Wl,--no-as-needed"

serverb: serverb.c
	buildserver -o $@ -f $< -s SERVICEB -v -f "-Wl,--no-as-needed"

client: client.c
	buildclient -o $@ -f $< -v -f "-Wl,--no-as-needed"

clean:
	-rm -f *.o ubbconfig tuxconfig client servera serverb ULOG.* stdout stderr access.*
