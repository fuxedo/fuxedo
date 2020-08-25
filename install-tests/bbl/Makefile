ifndef TUXDIR
$(error TUXDIR is not set)
endif

export PATH:=$(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH:=$(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG:=$(CURDIR)/tuxconfig
export FLDTBLDIR32:=$(TUXDIR)/udataobj
export FIELDTBLS32:=tpadm

check: server  tuxconfig
	-rm -f ULOG.*
	-tmipcrm -y
	tmboot -y
	echo "SRVCNM\t.TMIB\nTA_CLASS\tT_DOMAIN\nTA_OPERATION\tGET\n\n" | ud32
	tmshutdown -y

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
