# Higher-level compatibility tests driven through tuxedo-python.
#
# Requires:
#   TUXDIR           - fuxedo (or Oracle Tuxedo) installation
#   TUXEDO_PYTHON    - path to the tuxedo-python bindings source
#                      (default: ../private/reference/tuxedo-python)
#
# Usage:
#   make -C pytests check

ifndef TUXDIR
$(error TUXDIR is not set)
endif

TUXEDO_PYTHON ?= ../private/reference/tuxedo-python
export TUXEDO_PYTHON
PYBUILD := $(shell ls -d $(abspath $(TUXEDO_PYTHON))/build/lib.* 2>/dev/null | head -1)

export PATH := $(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH := $(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export PYTHONPATH := $(PYBUILD):$(CURDIR)
export FLDTBLDIR32 := $(TUXDIR)/udataobj
export FIELDTBLS32 := tpadm

TESTS := $(sort $(wildcard test_*.py))

.PHONY: check module $(TESTS)

check: module $(TESTS)
	@echo "pytests: all tests passed"

module:
	@if [ -z "$(PYBUILD)" ]; then \
		echo "building tuxedo-python module from $(TUXEDO_PYTHON)"; \
		(cd $(TUXEDO_PYTHON) && TUXDIR=$(TUXDIR) python3 setup.py build) || exit 1; \
	fi

$(TESTS): module
	python3 $@

clean:
	rm -rf scratch __pycache__
