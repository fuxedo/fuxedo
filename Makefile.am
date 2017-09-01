ACLOCAL_AMFLAGS = -I m4
AM_CXXFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/third_party/catch

include_HEADERS = include/xatmi.h include/fml32.h

lib_LTLIBRARIES = src/libfuxedo.la

src_libfuxedo_la_SOURCES = src/xatmi.cpp src/mem.cpp src/fml32.cpp src/expr.cpp
src_libfuxedo_la_LDFLAGS = 

TESTS = tests/xatmi tests/fml32
check_PROGRAMS = $(TESTS)
AM_TESTS_ENVIRONMENT = FLDTBLDIR32=.:src:tests FIELDTBLS32=dummy,fields

tests_xatmi_SOURCES = tests/xatmi.cpp
tests_xatmi_LDADD = src/libfuxedo.la -lpthread

tests_fml32_SOURCES = tests/fml32.cpp
tests_fml32_LDADD = src/libfuxedo.la -lpthread

clang-format:
	clang-format -style=Google -i $(top_srcdir)/include/*.h $(top_srcdir)/src/*.cpp $(top_srcdir)/src/*.h $(top_srcdir)/tests/*.cpp
