default:
	make clean
	make check
	make clean

check:
	make -C unit
	make -C build
	make -C server
	make -C tmshutdown
	make -C tpinit
	make -C tpcall
	make -C tpacall
	make -C tpadvertise
	make -C tpforward
	make -C blocktime
	make -C txnull
	make -C bbl

clean:
	make -C unit clean
	make -C build clean
	make -C server clean
	make -C tmshutdown clean
	make -C tpinit clean
	make -C tpcall clean
	make -C tpacall clean
	make -C tpadvertise clean
	make -C tpforward clean
	make -C blocktime clean
	make -C txnull clean
	make -C bbl clean


