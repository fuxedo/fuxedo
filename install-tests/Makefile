default:
	make clean
	make check
	make clean

check:
	mkdir -p tmp
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
	make -C conv
	make -C txnull
	make -C qmtx
	make -C restart
	make -C qsample
	make -C bbl
	make -C flags

# The subset that also passes against real Oracle Tuxedo installations
# (run by CI with TUXDIR pointing at an Oracle home). build and qmtx
# build a TMS via buildtms, which fails to link Oracle's prebuilt TMS.o
# on modern toolchains; txnull links against fuxedo's own library;
# restart and qsample exercise fuxedo-specific pieces (/Q layout,
# restart bookkeeping). conv runs fuxedo-only for now: Oracle 12.2.2
# answers tpconnect to an advertised conversational service with
# TPENOENT on this home (pristine boot, static or dynamic advertise),
# an open platform question - see notes/flags-investigation.md.
# Verified against Oracle 12.2.2.
ORACLE_DIRS = unit view server tmshutdown tpinit tpcall tpacall \
              tpadvertise tpforward blocktime bbl eventbr flags

# 22.1 narrows to the compile-and-API suites: its tmipcrm refuses to
# run without an attached bulletin board, which wedges the boot-oriented
# directories' cleanup steps, and tmboot returns before the bulletin
# board accepts clients; see the oracle-version-differences note.
ORACLE_221_DIRS = unit view

# spill files and other temp artifacts stay out of the system /tmp:
# the suites point TMPDIR at a scratch dir (Tuxedo honors it per-process)
export TMPDIR := $(abspath tmp)

check-compat:
	mkdir -p tmp
	@for d in $(ORACLE_DIRS); do \
		timeout 300 $(MAKE) -C $$d || exit 1; \
	done

check-compat-221:
	mkdir -p tmp
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
	make -C eventbr clean
	make -C conv clean
	make -C txnull
	make -C qmtx
	make -C restart
	make -C qsample clean
	make -C bbl clean


