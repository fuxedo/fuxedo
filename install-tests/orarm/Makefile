# clean is expected to work in a bare shell too
ifeq ($(filter clean,$(MAKECMDGOALS)),)
ifndef TUXDIR
$(error TUXDIR is not set)
endif
ifndef ORACLE_HOME
$(error ORACLE_HOME is not set)
endif
endif

include ../mk/domain.mk

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

# extra @PLACEHOLDERS@ expanded by ../mk/domain.mk
UBB_EXTRA_SED = | sed "s:@IPCKEY@:$(IPCKEY):g" \
	  | sed "s|@OPENINFO@|$(OPENINFO)|g"

check: scrub db-setup orasvr client TMS_ORACLE TMS_QM_ORARM tuxconfig crlog qsetup
	-tmipcrm -y 2>/dev/null
	tmboot -y
	sleep 3
	./client
	-tmshutdown -y
	$(MAKE) verify

# queue space for the two-RM scenarios: JOBS feeds TMQFORWARD (generous
# retries for the redeliver-on-visibility semantics), RAWQ carries the
# client's own probe messages
qsetup:
	rm -rf QUE
	printf 'crdl %s 0 400\nqspacecreate -n 100B\nQSPACE\n46001\n100\n6\n4\n9\n3\nERRQ\nn\n16\nqopen QSPACE\nqcreate JOBS fifo none 10 2 100%% 0%% -\nqcreate RAWQ fifo none 0 0 100%% 0%% -\n' "$$(pwd)/QUE" | qmadmin "$$(pwd)/QUE"

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
	@echo "expected: s1=1 s3=1 s5=1 s2=0 s4=0 s6=0 s7=1 s8=0 q=5/5 qr=3/3"
	@echo "pins verified live against Oracle 12.2.2: s5 aborts a"
	@echo "server-begun tx left open at tpreturn + answers TPESVCERR;"
	@echo "s6 times the transaction out (TPETIME) and rolls the row back"

# the platform flow: allocate the log device, then initialize it
crlog: tuxconfig
	rm -rf TLOG QUE
	printf 'crdl -b 200 -z %s\ncrlog -m tuxapp\nq\n' "$(CURDIR)/TLOG" | tmadmin

orasvr_gen.c: server.pc orarm.h
	gcc -E -P -std=gnu89 -x c \
	  -D'_Float128=long double' -D'__float128=long double' \
	  -D'__attribute__(x)=' -D'__asm(x)=' -D'__asm__(x)=' \
	  -D'__extension__=' -D'__inline=' -D'__const=' -D'__signed__=' \
	  -D'__restrict=' -D'__volatile=' -D'__restrict__=' \
	  -I$(CURDIR) -I$(TUXDIR)/include server.pc -o server.flat.c
	$(ORACLE_HOME)/bin/proc iname=server.flat.c oname=$@ \
	  code=ansi_c mode=ansi select_error=no > server.proclog 2>&1
	@grep -qE "Syntax error|PCC-" server.proclog && \
	  { tail -5 server.proclog; exit 1; } || true

orasvr: orasvr_gen.c
	buildserver -o $@ -r Oracle_XA -f $< -s INSERT -s INSERTFAIL \
	  -s INSERTOWN -s SUBMIT -s JOBS \
	  -v -f "-DSQLCODE=sqlca.sqlcode" -f "-Wl,--no-as-needed"

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
	rm -f ubbconfig tuxconfig orasvr orasvr_gen.c server.flat.c \
	  server.proclog client TMS_ORACLE TMS_QM_ORARM orarm.h \
	  orarm.fml ULOG.* stdout stderr
	rm -rf TLOG QUE

.PHONY: check verify crlog clean ubbconfig scrub db-setup qsetup
