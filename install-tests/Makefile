default:
	make clean
	make check
	make clean

check:
	make -C unit
	make -C view 
	make -C build
	make -C server
	make -C tmshutdown
	make -C tpinit
	make -C tpcall
	make -C tpacall
	make -C tpadvertise
	make -C tpforward
	make -C blocktime
	make -C eventbr
	make -C txnull
	make -C qmtx
	make -C restart
	make -C qsample
	make -C bbl

# The subset that also passes against real Oracle Tuxedo installations
# (run by CI with TUXDIR pointing at an Oracle home). build and qmtx
# build a TMS via buildtms, which fails to link Oracle's prebuilt TMS.o
# on modern toolchains; txnull links against fuxedo's own library;
# restart and qsample exercise fuxedo-specific pieces (/Q layout,
# restart bookkeeping). Verified against Oracle 12.2.2.
ORACLE_DIRS = unit view server tmshutdown tpinit tpcall tpacall \
              tpadvertise tpforward blocktime bbl eventbr

# 22.1 narrows to the compile-and-API suites: its tmipcrm refuses to
# run without an attached bulletin board, which wedges the boot-oriented
# directories' cleanup steps, and tmboot returns before the bulletin
# board accepts clients. See private/notes/oracle-version-differences.md.
ORACLE_221_DIRS = unit view

check-compat:
	@for d in $(ORACLE_DIRS); do \
		timeout 300 $(MAKE) -C $$d || exit 1; \
	done

check-compat-221:
	@for d in $(ORACLE_221_DIRS); do \
		timeout 300 $(MAKE) -C $$d || exit 1; \
	done

clean:
	make -C unit clean
	make -C view clean
	make -C build clean
	make -C server clean
	make -C tmshutdown clean
	make -C tpinit clean
	make -C tpcall clean
	make -C tpacall clean
	make -C tpadvertise clean
	make -C tpforward clean
	make -C blocktime clean
	make -C txnull
	make -C qmtx
	make -C restart
	make -C qsample clean
	make -C bbl clean


