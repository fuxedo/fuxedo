ifndef TUXDIR
$(error TUXDIR is not set)
endif

export PATH:=$(TUXDIR)/bin:$(PATH)
export LD_LIBRARY_PATH:=$(TUXDIR)/lib:$(LD_LIBRARY_PATH)
export TUXCONFIG:=$(CURDIR)/tuxconfig


check:
	! viewc32 -h
	! viewc32 -n invalid0.v
	! viewc32 -n invalid1.v
	! viewc32 -n invalid2.v
	viewc32 -n valid0.v
	viewc32 -n valid1.v
	viewc32 -n valid2.v

clean:
