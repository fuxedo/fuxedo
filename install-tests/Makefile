default:
	make clean
	make check
	make clean

check:
	make -C unit
	make -C build
	make -C server
	make -C tpinit
	make -C tpcall
	make -C tpacall
	make -C tpadvertise
	make -C tpforward
	make -C blocktime

clean:
	make -C unit clean
	make -C build clean
	make -C server clean
	make -C tpinit clean
	make -C tpcall clean
	make -C tpacall clean
	make -C tpadvertise clean
	make -C tpforward clean
	make -C blocktime clean


