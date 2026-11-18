ifndef TUXDIR
$(error TUXDIR is not set)
endif

export PATH:=$(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH:=$(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG:=$(CURDIR)/tuxconfig

check: restartme client tuxconfig
	-rm -f ULOG.*
	-tmshutdown -y 2>/dev/null
	-tmipcrm -y 2>/dev/null
	-sleep 2
	tmboot -y
	sleep 3
	./client
	kill -9 `pgrep -x restartme | head -1`
	sh waitup.sh
	./client
	kill -9 `pgrep -x restartme | head -1`
	sh waitup.sh
	./client
	# MAXGEN=2 spent within GRACE: the third death is final
	kill -9 `pgrep -x restartme | head -1`
	sleep 5
	!(./client 2>/dev/null)
	# fuxedo ULOG assertions (own wording; Oracle wording differs)
	grep -E -q '[!:]BBL\..*: Restarting restartme -g 1 -i 1, generation 2' ULOG.*
	grep -E -q '[!:]BBL\..*: Restarting restartme -g 1 -i 1, generation 3' ULOG.*
	grep -E -q '[!:]BBL\..*: Not restarting restartme -g 1 -i 1' ULOG.*
	tmshutdown -y
	# a clean shutdown must not trigger a restart
	! grep -E -q '[!:]BBL\..*: Restarting restartme -g 1 -i 1, generation 4' ULOG.*

ubbconfig: ubbconfig.in
	cat $< \
          | sed s:@TUXDIR@:$(TUXDIR):g \
          | sed s:@UNAME@:`uname -n`:g \
          | sed s:@CURDIR@:$(CURDIR):g > $@

tuxconfig: ubbconfig
	tmloadcf -y $<

restartme: server.c
	buildserver -o $@ -f $< -s PING -v -f "-Wl,--no-as-needed"

client: client.c
	buildclient -o $@ -f $< -v -f "-Wl,--no-as-needed"

clean:
	-tmshutdown -y 2>/dev/null
	-tmipcrm -y 2>/dev/null
	-sleep 1
	-rm -rf ubbconfig tuxconfig restartme client ULOG.* stdout stderr access.* core || true
