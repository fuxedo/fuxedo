ifndef TUXDIR
$(error TUXDIR is not set)
endif

export PATH:=$(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH:=$(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG:=$(CURDIR)/tuxconfig

# fuxedo runs the transactional cases with its built-in null resource
# manager; Oracle here covers the no-TMS semantics (TPESVCERR on failed
# forward, TPNOREPLY release, tpbegin/tpcommit/tpabort returning 1)
ifneq ($(wildcard $(TUXDIR)/lib/libfuxedo.so),)
FUXEDO = yes
TLOGDEV = TLOGDEVICE="$(CURDIR)/tlog" TLOGNAME=tlog
GROUPLINE = GROUP1 LMID=tuxapp GRPNO=1 TMSNAME=TMSNULL
else
FUXEDO =
TLOGDEV =
GROUPLINE = GROUP1 LMID=tuxapp GRPNO=1
endif

check: servera serverb serverc client txprop TMSNULL tuxconfig
	-rm -f ULOG.*
	-tmipcrm -y
ifdef FUXEDO
	mkdir -p tlog
endif
	-tmboot -y
	sleep 3 # 22.1 returns before the bulletin board is ready
	./client
ifdef FUXEDO
	./txprop
endif
	tmshutdown -y
	-tmshutdown -y -k KILL
	grep -q ':TEST: SERVICEA called' ULOG.*
	grep -q ':TEST: SERVICEB called' ULOG.*
	grep -q ':TEST: FAILSERVICE called' ULOG.*
	grep -q ':TEST: LOCALSERVICE called' ULOG.*
	grep -q ':TEST: TXFWDFAIL called' ULOG.*
	grep -q ':TEST: FWDNOREPLY called' ULOG.*
ifdef FUXEDO
	# the forward carries the caller's transaction; TPNOTRAN exempts the
	# target (asserted from TXSVC's tpgetlev() log lines)
	grep -q ':TEST: TXSVC called lev=1' ULOG.*
	grep -q ':TEST: TXSVC called lev=0' ULOG.*
endif

ubbconfig: ubbconfig.in
	cat $< \
          | sed s:@TUXDIR@:$(TUXDIR):g \
          | sed s:@UNAME@:`uname -n`:g \
          | sed s:@CURDIR@:$(CURDIR):g \
          | sed 's|@TLOGDEV@|$(TLOGDEV)|' \
          | sed 's|@GROUPLINE@|$(GROUPLINE)|' > $@

tuxconfig: ubbconfig
	tmloadcf -y $<

servera: servera.c
	buildserver -o $@ -f $< -s SERVICEA -s FAILSERVICE -s LOCALSERVICE -s TXFWDFAIL -s FWDTX -s FWDNOTRAN -s FWDNOREPLY -s FWDNOSLOW -v -f "-Wl,--no-as-needed"

serverb: serverb.c
	buildserver -o $@ -f $< -s SERVICEB -s TXSVC -s SLOWSVC -v -f "-Wl,--no-as-needed"

serverc: serverc.c
	buildserver -o $@ -f $< -s NOTXSSVC -v -f "-Wl,--no-as-needed"

# fuxedo: null RM ships in the box (a group needs a TMS for services to
# join transactions). Oracle runs this fixture without a TMS.
TMSNULL:
ifdef FUXEDO
	CC="$(CXX)" buildtms -o TMSNULL -r NONE -v
endif

client: client.c
	buildclient -o $@ -f $< -v -f "-Wl,--no-as-needed"

txprop: txprop.c
	buildclient -o $@ -f $< -v -f "-Wl,--no-as-needed"

# behavior probe: prints observed outcomes instead of asserting, used to
# pin Oracle's semantics (run manually against an Oracle home)
probe: probe.c
	buildclient -o $@ -f $< -v -f "-Wl,--no-as-needed"

clean:
	-rm -f *.o ubbconfig tuxconfig client txprop probe servera serverb serverc TMSNULL ULOG.* stdout stderr access.*
	-rm -rf tlog
