ACLOCAL_AMFLAGS = -I m4
AM_CXXFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/third_party/catch -I$(top_srcdir)/third_party/clara -g -Wall -Wextra
AM_CFLAGS = -I$(top_srcdir)/include

include_HEADERS = include/atmi.h include/xatmi.h include/fml32.h include/userlog.h include/atmidefs.h include/xa.h include/tpadm.h

lib_LTLIBRARIES = src/libfuxedo.la

src_libfuxedo_la_SOURCES = src/xatmi.cpp src/mem.cpp src/fml32.cpp src/expr.cpp src/mib.cpp src/server.cpp src/ubb2mib.cpp src/userlog.cpp src/ipc.cpp src/base64.cpp src/client.cpp src/todo.cpp src/misc.cpp
src_libfuxedo_la_LDFLAGS = 

bin_PROGRAMS = src/mkfldhdr32 src/ud32 src/tmloadcf src/fux src/tmipcrm src/buildserver src/buildclient

src_buildserver_SOURCES = src/buildserver.cpp

src_buildclient_SOURCES = src/buildclient.cpp

src_mkfldhdr32_SOURCES = src/mkfldhdr32.cpp
src_mkfldhdr32_LDADD = src/libfuxedo.la -lpthread

src_ud32_SOURCES = src/ud32.cpp
src_ud32_LDADD = src/libfuxedo.la -lpthread

src_tmloadcf_SOURCES = src/tmloadcf.cpp
src_tmloadcf_LDADD = src/libfuxedo.la -lpthread

src_fux_SOURCES = src/fux.cpp
src_fux_LDADD = src/libfuxedo.la -lpthread

src_tmipcrm_SOURCES = src/tmipcrm.cpp
src_tmipcrm_LDADD = src/libfuxedo.la -lpthread

TESTS = tests/xatmi tests/fml32 tests/mib tests/ipcq tests/base64 tests/userlog
check_PROGRAMS = $(TESTS)

AM_TESTS_ENVIRONMENT = FLDTBLDIR32=.:src:tests FIELDTBLS32=dummy,fields

tests_userlog_SOURCES = tests/userlog.cpp tests/tests-main.cpp
tests_userlog_LDADD = src/libfuxedo.la -lpthread

tests_base64_SOURCES = tests/base64.cpp tests/tests-main.cpp
tests_base64_LDADD = src/libfuxedo.la -lpthread

tests_ipcq_SOURCES = tests/ipcq.cpp tests/tests-main.cpp
tests_ipcq_LDADD = src/libfuxedo.la -lpthread

tests_xatmi_SOURCES = tests/xatmi.cpp tests/tests-main.cpp
tests_xatmi_LDADD = src/libfuxedo.la -lpthread

tests_fml32_SOURCES = tests/fml32.cpp tests/tests-main.cpp
tests_fml32_LDADD = src/libfuxedo.la -lpthread

tests_mib_SOURCES = tests/mib.cpp tests/tests-main.cpp
tests_mib_LDADD = src/libfuxedo.la -lpthread

clang-format:
	clang-format -style=Google -i $(top_srcdir)/include/*.h $(top_srcdir)/src/*.cpp $(top_srcdir)/src/*.h $(top_srcdir)/tests/*.cpp
