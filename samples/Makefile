# Runs Oracle Tuxedo C samples against fuxedo.
#
# The sample sources are NOT part of fuxedo (they are Oracle copyrighted
# material); they are copied from an existing Oracle Tuxedo installation at
# test time and never committed.
#
# Requires:
#   TUXDIR      - fuxedo (or Oracle) installation used to build and run
#   SAMPLE_HOME - Oracle Tuxedo installation that ships samples/atmi
#
# Usage:
#   make -C samples check

ifndef TUXDIR
$(error TUXDIR is not set)
endif

ifndef SAMPLE_HOME
$(error SAMPLE_HOME is not set)
endif

export TUXDIR
export SAMPLE_HOME
export PATH := $(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH := $(TUXDIR)/lib:$(LD_LIBRARY_PATH)

SAMPLES := simpapp

check: $(SAMPLES)
	@echo "samples: all passed"

$(SAMPLES):
	$(MAKE) -C $@ check

clean:
	-rm -rf $(addsuffix /work,$(SAMPLES))

.PHONY: check clean $(SAMPLES)
