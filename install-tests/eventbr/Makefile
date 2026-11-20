ifndef TUXDIR
$(error TUXDIR is not set)
endif

export PATH:=$(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH:=$(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG:=$(CURDIR)/tuxconfig
# the FML32 filter in the selftest resolves TA_OPERATION by name
export FLDTBLDIR32:=$(TUXDIR)/udataobj
export FIELDTBLS32:=tpadm

ifneq ($(wildcard $(TUXDIR)/lib/libfuxedo.so),)
FUXEDO = yes
else
FUXEDO =
endif

check: client tuxconfig
	-rm -f ULOG.* hold.out bcast.*
	-tmipcrm -y
	-tmboot -y
	sleep 3
	./client self
	( ./client blisten - bcast.D & ld=$$!; ./client blisten - bcast.E & le=$$!; sleep 2; ./client btrigger all; wait $$ld; wait $$le )
	grep -q '^GOT toall$$' bcast.D
	grep -q '^GOT toall$$' bcast.E
ifdef FUXEDO
	( ./client esub > esub.out & es=$$!; sleep 2; echo printevents | tmadmin -r > pe.out; wait $$es )
	grep -q 'TEST_EVENT' pe.out
	( ./client blisten listenerA bcast.A & la=$$!; ./client blisten listenerC bcast.C & lc=$$!; sleep 2; ./client btrigger targeted; wait $$la; wait $$lc )
	grep -q '^GOT targeted$$' bcast.A
	grep -q '^NONE$$' bcast.C
endif
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
	-rm -f ubbconfig tuxconfig client ULOG.* stdout stderr hold.out hold.pid bcast.* esub.out pe.out tmusrevt.dat
