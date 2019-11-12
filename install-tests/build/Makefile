ifndef TUXDIR
$(error TUXDIR is not set)
endif

export PATH:=$(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH:=$(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG:=$(CURDIR)/tuxconfig

check:
	buildserver -h
	buildclient -h
	buildtms -h
	!(buildserver -zinvalid_option)
	!(buildclient -zinvalid_option)
	!(buildtms -zinvalid_option)
	!(buildserver -o server -f server.c -s SERVICE -v -f "-Wl,--no-as-needed" -r does_not_exist)
#	buildtms -o server -v -r NONE
	buildserver -o server -f server.c -s SERVICE -v -f "-Wl,--no-as-needed" -r NONE
	buildserver -o server -f server.c -s SERVICE -v -f "-Wl,--no-as-needed"
	buildserver -o server -f server.c -s ANOTHER:SERVICE -v -f "-Wl,--no-as-needed"
	buildclient -o client -f client.c -v -f "-Wl,--no-as-needed"
	./server -h
	./server -v
	!(./server -zinvalid_option)

clean:
	-rm -f *.o client server ULOG.* stdout stderr access.*
