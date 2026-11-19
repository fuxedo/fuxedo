ifndef TUXDIR
$(error TUXDIR is not set)
endif

export PATH:=$(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH:=$(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG:=$(CURDIR)/tuxconfig

check: test-fml32 test-expr test-view32 test-client test-hardening tuxconfig
	FLDTBLDIR32=../../tests FIELDTBLS32=fields ./test-fml32 -r console
	FLDTBLDIR32=../../tests FIELDTBLS32=fields ./test-expr -r console
	FLDTBLDIR32=../../tests FIELDTBLS32=fields VIEWDIR32=. VIEWFILES32=view32test.V ./test-view32 -r console
	./test-client -r console
	FLDTBLDIR32=../../tests FIELDTBLS32=fields VIEWDIR32=. VIEWFILES32=view32test.V ./test-hardening -r console

ubbconfig: ubbconfig.in
	cat $< \
          | sed s:@TUXDIR@:$(TUXDIR):g \
          | sed s:@UNAME@:`uname -n`:g \
          | sed s:@CURDIR@:$(CURDIR):g > $@

tuxconfig: ubbconfig
	tmloadcf -y $<

CFLAGS=-I../../third_party/catch/ -I../../tests -I$(TUXDIR)/include

fml32.o: ../../tests/compat/fml32.cpp
	$(CXX) -c -o $@ $< $(CFLAGS)
expr.o: ../../tests/compat/expr.cpp
	$(CXX) -c -o $@ $< $(CFLAGS)
view32.o: ../../tests/compat/view32.cpp view32test.h
	$(CXX) -c -o $@ $< $(CFLAGS) -I.
tests-main.o: ../../tests/tests-main.cpp
	$(CXX) -c -o $@ $< $(CFLAGS)
client.o: client.cpp
	$(CXX) -c -o $@ $< $(CFLAGS)

view32test.h view32test.V: ../../tests/compat/view32test.v
	FLDTBLDIR32=../../tests FIELDTBLS32=fields viewc32 -d . ../../tests/compat/view32test.v

test-fml32: fml32.o tests-main.o
	CC="$(CXX)" buildclient -o $@ -f "tests-main.o fml32.o" -v -f "-Wl,--no-as-needed"

test-expr: expr.o tests-main.o
	CC="$(CXX)" buildclient -o $@ -f "tests-main.o expr.o" -v -f "-Wl,--no-as-needed"

test-view32: view32.o tests-main.o
	CC="$(CXX)" buildclient -o $@ -f "tests-main.o view32.o" -v -f "-Wl,--no-as-needed"

test-client: client.o tests-main.o
	CC="$(CXX)" buildclient -o $@ -f "tests-main.o client.o" -v -f "-Wl,--no-as-needed"

hardening.o: ../../tests/compat/hardening.cpp
	$(CXX) -c -o $@ $< $(CFLAGS)

test-hardening: hardening.o tests-main.o
	CC="$(CXX)" buildclient -o $@ -f "tests-main.o hardening.o" -v -f "-Wl,--no-as-needed"

clean:
	-rm -f *.o ubbconfig tuxconfig test-client ULOG.* stdout stderr access.* *.o test-fml32 test-expr test-view32 test-hardening view32test.h view32test.V
