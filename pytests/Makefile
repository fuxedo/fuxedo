# Higher-level compatibility tests driven through tuxedo-python.
#
# Requires:
#   TUXDIR           - fuxedo (or Oracle Tuxedo) installation
#   TUXEDO_PYTHON    - path to the tuxedo-python bindings source
#                      (default: ../../tuxedo-python, the bindings clone)
#
# Usage:
#   make -C pytests check

ifndef TUXDIR
$(error TUXDIR is not set)
endif

TUXEDO_PYTHON ?= ../../tuxedo-python
export TUXEDO_PYTHON
PYV := $(shell python3 -c 'import sys; print(f"{sys.version_info.major}{sys.version_info.minor}")')
PYBUILD := $(shell ls -d $(abspath $(TUXEDO_PYTHON))/build/lib.*cpython-$(PYV) 2>/dev/null | head -1)

export PATH := $(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH := $(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export PYTHONPATH := $(PYBUILD):$(CURDIR)
export FLDTBLDIR32 := $(TUXDIR)/udataobj
# 22.1 moved SRVCNM and friends from tpadm to Usysfl32
ifneq ($(wildcard $(TUXDIR)/udataobj/Usysfl32),)
export FIELDTBLS32 := tpadm,Usysfl32
else
export FIELDTBLS32 := tpadm
endif

TESTS := $(sort $(wildcard test_*.py))

.PHONY: check module $(TESTS)

check: module $(TESTS)
	@echo "pytests: all tests passed"

module:
	@if [ -z "$(PYBUILD)" ] || [ ! -f $(TUXEDO_PYTHON)/build/.tuxdir ] || \
	    [ "$$(cat $(TUXEDO_PYTHON)/build/.tuxdir 2>/dev/null)" != "$(TUXDIR)" ]; then \
		echo "building tuxedo-python module from $(TUXEDO_PYTHON) against $(TUXDIR)"; \
		rm -rf $(TUXEDO_PYTHON)/build/temp.*; \
		(cd $(TUXEDO_PYTHON) && TUXDIR=$(TUXDIR) python3 setup.py build) || exit 1; \
		echo "$(TUXDIR)" > $(TUXEDO_PYTHON)/build/.tuxdir; \
	fi

$(TESTS): module
	PYTHONPATH=$$(ls -d $(abspath $(TUXEDO_PYTHON))/build/lib.*cpython-$(PYV) 2>/dev/null | head -1):$(CURDIR) python3 $@

clean:
	rm -rf scratch __pycache__
