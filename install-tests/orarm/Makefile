# clean is expected to work in a bare shell too
ifeq ($(filter clean,$(MAKECMDGOALS)),)
ifndef TUXDIR
$(error TUXDIR is not set)
endif
ifndef ORACLE_HOME
$(error ORACLE_HOME is not set)
endif
endif

export ORACLE_HOME
export PATH := $(TUXDIR)/bin:$(ORACLE_HOME)/bin:$(PATH)
export LD_LIBRARY_PATH := $(TUXDIR)/lib:$(ORACLE_HOME)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG := $(CURDIR)/tuxconfig
export FLDTBLDIR32 := $(CURDIR)
export FIELDTBLS32 := orarm.flds
export ORARM_USER := fuxedo
export ORARM_PASS := fuxedo
export ORARM_DB := //localhost:1521/XEPDB1
export ORARM_DB_ALIAS := XEPDB1
export TNS_ADMIN := $(CURDIR)
# the platform strips "<switch_name>:" before xa_open; the xa_info
# itself keeps its own "Oracle_XA+" prefix (same on Oracle and fuxedo)
export OPENINFO := Oracle_XA:Oracle_XA+ACC=P/$(ORARM_USER)/$(ORARM_PASS)+SqlNet=XEPDB1+Threads=true+SesTm=60
# Oracle's prebuilt lib/TMS.o needs a non-PIE link on modern
# toolchains; scoped to the buildtms invocation, not the whole build

IPCKEY := $(shell echo $$(( (RANDOM % 20000) + 41000 )))

check: scrub db-setup orasvr client TMS_ORACLE TMS_QM_ORARM tuxconfig crlog qsetup
	-tmipcrm -y 2>/dev/null
	tmboot -y
	sleep 3
	./client
	-tmshutdown -y
	$(MAKE) verify

# queue space for the two-RM scenarios
qsetup:
	rm -rf QUE
	printf 'crdl %s 0 400\nqspacecreate -n 100B\nQSPACE\n46001\n100\n6\n4\n9\n3\nERRQ\nn\n16\nqopen QSPACE\nqcreate JOBS fifo none 2 30 100%% 0%% -\n' "$$(pwd)/QUE" | qmadmin "$$(pwd)/QUE"

TMS_QM_ORARM:
	CFLAGS=-no-pie buildtms -o $@ -r TUXEDO/QM -v

# (re)create the test table; the instance may have been rebuilt
db-setup:
	@$(ORACLE_HOME)/bin/sqlplus -s $(ORARM_USER)/$(ORARM_PASS)@$(ORARM_DB) @setup.sql > /dev/null

# kill anything from a previous failed run, then free the ipc keys
scrub:
	-tmshutdown -y 2>/dev/null
	-tmipcrm -y 2>/dev/null
	-pkill -x TMS_ORACLE 2>/dev/null
	-pkill -x orasvr 2>/dev/null
	-pkill -x BBL 2>/dev/null
	-for id in $$(ipcs -q | awk 'NR>3{print $$2}'); do ipcrm -q $$id 2>/dev/null; done
	-for id in $$(ipcs -s | awk 'NR>3{print $$2}'); do ipcrm -s $$id 2>/dev/null; done
	-for id in $$(ipcs -m | awk 'NR>3{print $$2}'); do ipcrm -m $$id 2>/dev/null; done
	@true

# row visibility, asserted from outside the application
verify:
	@$(ORACLE_HOME)/bin/sqlplus -s $(ORARM_USER)/$(ORARM_PASS)@$(ORARM_DB) @verify.sql
	@echo "expected: s1=1 s3=1 s5=1 s2=0 s4=0 s6=0 s7=1 s8=0"

ubbconfig: ubbconfig.in
	cat $< \
	  | sed "s:@TUXDIR@:$(TUXDIR):g" \
	  | sed "s:@UNAME@:$(shell uname -n):g" \
	  | sed "s:@CURDIR@:$(CURDIR):g" \
	  | sed "s:@IPCKEY@:$(IPCKEY):g" \
	  | sed "s|@OPENINFO@|$(OPENINFO)|g" > $@

tuxconfig: ubbconfig
	tmloadcf -y $<

# the platform flow: allocate the log device, then initialize it
crlog: tuxconfig
	rm -rf TLOG QUE
	printf 'crdl -b 200 -z %s\ncrlog -m tuxapp\nq\n' "$(CURDIR)/TLOG" | tmadmin

orasvr: server.c orarm.h
	buildserver -o $@ -r Oracle_XA -f $< -s INSERT -s INSERTFAIL \
	  -s INSERTOWN \
	  -f "-I$(ORACLE_HOME)/sdk/include -L$(ORACLE_HOME)/lib -lclntsh" \
	  -v -f "-Wl,--no-as-needed"

client: client.c orarm.h
	buildclient -o $@ -f $< -f "-I$(ORACLE_HOME)/sdk/include" \
	  -v -f "-Wl,--no-as-needed"

TMS_ORACLE:
	CFLAGS=-no-pie buildtms -o $@ -r Oracle_XA -v

orarm.h: orarm.flds
	mkfldhdr32 $<
	@test -f $@ || mv orarm.flds.h $@

clean:
	-tmshutdown -y 2>/dev/null
	-tmipcrm -y 2>/dev/null
	rm -f ubbconfig tuxconfig orasvr client TMS_ORACLE TMS_QM_ORARM orarm.h \
	  orarm.fml ULOG.* stdout stderr
	rm -rf TLOG QUE

.PHONY: check verify crlog clean ubbconfig scrub db-setup qsetup
