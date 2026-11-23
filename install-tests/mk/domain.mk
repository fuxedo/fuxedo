# Shared scaffolding for the install-tests domains: toolchain
# environment and the ubbconfig template expansion that every test
# repeats. Include as ../mk/domain.mk from a test directory.
#
# A test with extra @PLACEHOLDERS@ in its ubbconfig.in extends the
# pipeline before the include, e.g.:
#   UBB_EXTRA_SED = | sed 's|@THREADLINE@|$(THREADLINE)|'

ifndef TUXDIR
$(error TUXDIR is not set)
endif

export PATH := $(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH := $(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG := $(CURDIR)/tuxconfig

ubbconfig: ubbconfig.in
	cat $< \
	  | sed s:@TUXDIR@:$(TUXDIR):g \
	  | sed s:@UNAME@:`uname -n`:g \
	  | sed s:@CURDIR@:$(CURDIR):g $(UBB_EXTRA_SED) > $@

tuxconfig: ubbconfig
	tmloadcf -y $<

# this file is included above the test's own targets; an empty
# .DEFAULT_GOAL makes the next target the test parses its default
.DEFAULT_GOAL :=
