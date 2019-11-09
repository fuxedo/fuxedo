ACLOCAL_AMFLAGS = -I m4

if CODE_COVERAGE_ENABLED 
gcov_flags = -fprofile-arcs -ftest-coverage
gcov_ldflags = -fprofile-arcs -ftest-coverage -lgcov
endif

AM_CXXFLAGS = -g -Wall -Wextra $(gcov_flags) \
              -I$(top_srcdir)/include \
              -I$(top_srcdir)/third_party/catch \
              -I$(top_srcdir)/third_party/clara \
              -I$(top_srcdir)/third_party/GSL

AM_LDFLAGS = $(gcov_ldflags)

include_HEADERS = include/atmi.h include/xatmi.h \
                  include/fml32.h \
                  include/userlog.h \
                  include/atmidefs.h \
                  include/xa.h \
                  include/tx.h \
                  include/tpadm.h

lib_LTLIBRARIES = src/libfuxedo.la

mylibdir = $(libdir)
mylib_DATA = src/tmserver.o

src_libfuxedo_la_SOURCES = src/xatmi.cpp src/mem.cpp \
                           src/fml32.cpp src/expr.cpp \
                           src/ctxt.cpp \
                           src/server.cpp src/client.cpp \
                           src/mib.cpp src/ubb2mib.cpp \
                           src/userlog.cpp \
                           src/ipc.cpp \
                           src/xa.cpp src/tx.cpp src/trx.cpp \
                           src/misc.cpp src/base64.cpp src/fields.cpp \
                           src/tpadmcall.cpp src/tmq.cpp

src_libfuxedo_la_LDFLAGS = -lpthread

bin_PROGRAMS = src/mkfldhdr32 src/ud32 \
               src/fux \
               src/tmipcrm \
               src/tmloadcf src/tmunloadcf \
               src/buildserver src/buildclient src/buildtms \
               src/tmboot src/tmshutdown \
               src/BBL src/TMS

src_buildtms_SOURCES = src/buildtms.cpp
src_buildserver_SOURCES = src/buildserver.cpp
src_buildclient_SOURCES = src/buildclient.cpp

src_tmboot_SOURCES = src/tmboot.cpp
src_tmboot_LDADD = src/libfuxedo.la

src_TMS_SOURCES = src/tmserver.cpp src/TMS.cpp
src_TMS_LDADD = src/libfuxedo.la

src_BBL_SOURCES = src/BBL.cpp
src_BBL_LDADD = src/libfuxedo.la -lpthread

src_tmshutdown_SOURCES = src/tmshutdown.cpp
src_tmshutdown_LDADD = src/libfuxedo.la

src_mkfldhdr32_SOURCES = src/mkfldhdr32.cpp
src_mkfldhdr32_LDADD = src/libfuxedo.la

src_ud32_SOURCES = src/ud32.cpp
src_ud32_LDADD = src/libfuxedo.la

src_tmloadcf_SOURCES = src/tmloadcf.cpp
src_tmloadcf_LDADD = src/libfuxedo.la

src_tmunloadcf_SOURCES = src/tmunloadcf.cpp
src_tmunloadcf_LDADD = src/libfuxedo.la

src_fux_SOURCES = src/fux.cpp
src_fux_LDADD = src/libfuxedo.la

src_tmipcrm_SOURCES = src/tmipcrm.cpp
src_tmipcrm_LDADD = src/libfuxedo.la

data_DATA = RM

TESTS = tests/xatmi tests/fml32 tests/expr tests/mib tests/ipcq tests/base64 tests/userlog tests/trx
check_PROGRAMS = $(TESTS)

AM_TESTS_ENVIRONMENT = FLDTBLDIR32=.:src:tests FIELDTBLS32=dummy,fields

tests_userlog_SOURCES = tests/userlog.cpp tests/tests-main.cpp
tests_userlog_LDADD = src/libfuxedo.la

tests_base64_SOURCES = tests/base64.cpp tests/tests-main.cpp
tests_base64_LDADD = src/libfuxedo.la

tests_ipcq_SOURCES = tests/ipcq.cpp tests/tests-main.cpp
tests_ipcq_LDADD = src/libfuxedo.la

tests_xatmi_SOURCES = tests/xatmi.cpp tests/tests-main.cpp
tests_xatmi_LDADD = src/libfuxedo.la

tests_fml32_SOURCES = tests/fml32.cpp tests/tests-main.cpp
tests_fml32_LDADD = src/libfuxedo.la

tests_expr_SOURCES = tests/expr.cpp tests/tests-main.cpp
tests_expr_LDADD = src/libfuxedo.la

tests_mib_SOURCES = tests/mib.cpp tests/tests-main.cpp
tests_mib_LDADD = src/libfuxedo.la

tests_trx_SOURCES = tests/trx.cpp tests/tests-main.cpp
tests_trx_LDADD = src/libfuxedo.la

clang-format:
	clang-format -style=Google -i \
                            $(top_srcdir)/include/*.h \
                            $(top_srcdir)/src/*.cpp \
                            $(top_srcdir)/src/*.h \
                            $(top_srcdir)/tests/*.cpp \
                            $(top_srcdir)/install-tests/*/*.c

installcheck-local:
	TUXDIR=$(prefix) make -C install-tests/build
	TUXDIR=$(prefix) make -C install-tests/server
	TUXDIR=$(prefix) make -C install-tests/tpcall
	TUXDIR=$(prefix) make -C install-tests/tpacall
	TUXDIR=$(prefix) make -C install-tests/tpforward
