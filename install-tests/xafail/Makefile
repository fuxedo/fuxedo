include ../mk/domain.mk

check: tmsxaf worker client tuxconfig tlog
	-tmshutdown -y 2>/dev/null
	-sleep 2
	tmboot -y
	sleep 3
	echo 0 > knob
	./client
	echo 1 > knob
	./client
	echo 0 > knob
	tmshutdown -y
	-tmshutdown -y -k KILL
	grep -q ':TEST: WORK called' ULOG.*

# register the scripted RM for the build (append; the DESTDIR install
# is disposable). The same switch is plain XA and can be registered on
# an Oracle home for a cross-platform run.
xafail.o: xafail.c
	$(CC) -c -o $@ $< -I$(TUXDIR)/include

rmline: xafail.o
	grep -q '^XAFAIL:' $(TUXDIR)/udataobj/RM || \
	  echo 'XAFAIL:xa_fail_switch:$(CURDIR)/xafail.o' >> $(TUXDIR)/udataobj/RM

tmsxaf: xafail.o rmline
	CC="$(CC)" buildtms -o TMSXAF -r XAFAIL -v

tlog:
	crdl -o 0 -s 100 tlog

worker: server.c
	buildserver -o $@ -f $< -s WORK -r XAFAIL -v -f "-Wl,--no-as-needed"

client: client.c
	buildclient -o $@ -f $< -v -f "-Wl,--no-as-needed"

clean:
	-tmshutdown -y 2>/dev/null
	-rm -rf ubbconfig tuxconfig tlog TMSXAF worker client ULOG.* stdout stderr access.* || true
