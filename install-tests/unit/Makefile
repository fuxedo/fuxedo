ifndef TUXDIR
$(error TUXDIR is not set)
endif

export PATH:=$(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH:=$(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG:=$(CURDIR)/tuxconfig

check: test-fml32 test-client tuxconfig
	FLDTBLDIR32=../../tests FIELDTBLS32=fields ./test-fml32 -r console
	./test-client

ubbconfig: ubbconfig.in
	cat $< \
          | sed s:@TUXDIR@:$(TUXDIR):g \
          | sed s:@UNAME@:`uname -n`:g \
          | sed s:@CURDIR@:$(CURDIR):g > $@

tuxconfig: ubbconfig
	tmloadcf -y $<

CFLAGS=-I../../third_party/catch/ -I$(TUXDIR)/include -std=c++11

fml32.o: ../../tests/fml32.cpp
	$(CXX) -c -o $@ $< $(CFLAGS)
tests-main.o: ../../tests/tests-main.cpp
	$(CXX) -c -o $@ $< $(CFLAGS)
client.o: client.cpp
	$(CXX) -c -o $@ $< $(CFLAGS)

test-fml32: fml32.o tests-main.o
	CC=$(CXX) buildclient -o $@ -f "tests-main.o fml32.o" -v -f "-Wl,--no-as-needed"

test-client: client.o tests-main.o
	CC=$(CXX) buildclient -o $@ -f "tests-main.o client.o" -v -f "-Wl,--no-as-needed"

clean:
	-rm -f *.o ubbconfig tuxconfig test-client ULOG.* stdout stderr access.* *.o test-fml32
