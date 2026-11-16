# Oracle Tuxedo simpapp sample running against fuxedo.
#
# Copies the sample from $(SAMPLE_HOME)/samples/atmi/simpapp into work/,
# adapts the configuration, builds with fuxedo tools, boots the application,
# runs the client and checks the reply.

SAMPLEDIR := $(SAMPLE_HOME)/samples/atmi/simpapp
WORK := work

check: run

prepare:
	@test -d $(SAMPLEDIR) || (echo "sample not found: $(SAMPLEDIR)"; exit 1)
	mkdir -p $(WORK)
	cp $(SAMPLEDIR)/simpserv.c $(SAMPLEDIR)/simpcl.c $(SAMPLEDIR)/ubbsimple $(WORK)/
	sed -i \
	  -e 's/<Replace with a valid IPC Key>/45671/' \
	  -e 's|<Replace with the current directory pathname>|$(abspath $(WORK))|' \
	  -e 's|<Replace with your TUXCONFIG Pathname>|$(abspath $(WORK))/tuxconfig|' \
	  -e 's|<Directory where TUXEDO is installed>|$(TUXDIR)|' \
	  -e 's/<Machine-name>/$(shell uname -n)/' \
	  -e 's/^tpsvrinit(int argc, char \*argv\[\])/int tpsvrinit(int argc, char *argv[])/' \
	  $(WORK)/ubbsimple
	sed -i 's/^tpsvrinit(int argc, char \*argv\[\])/int tpsvrinit(int argc, char *argv[])/' $(WORK)/simpserv.c

build: prepare
	buildclient -o $(WORK)/simpcl -f $(WORK)/simpcl.c -f "-Wl,--no-as-needed"
	buildserver -o $(WORK)/simpserv -f $(WORK)/simpserv.c -s TOUPPER -f "-Wl,--no-as-needed"

boot: build
	cd $(WORK) && TUXCONFIG=$(abspath $(WORK))/tuxconfig tmloadcf -y ubbsimple
	cd $(WORK) && TUXCONFIG=$(abspath $(WORK))/tuxconfig tmboot -y

run: boot
	cd $(WORK) && TUXCONFIG=$(abspath $(WORK))/tuxconfig ./simpcl "hello fuxedo" | tee output
	grep -q "HELLO FUXEDO" $(WORK)/output
	cd $(WORK) && TUXCONFIG=$(abspath $(WORK))/tuxconfig tmshutdown -y

clean:
	rm -rf $(WORK)

.PHONY: check prepare build boot run clean
