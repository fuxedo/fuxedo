ACLOCAL_AMFLAGS = -I m4

if CODE_COVERAGE_ENABLED 
gcov_flags = -fprofile-arcs -ftest-coverage
gcov_ldflags = -fprofile-arcs -ftest-coverage -lgcov
endif

AM_CXXFLAGS = -g -Wall -Wextra -Werror $(gcov_flags) \
              -I$(top_srcdir)/include \
              -isystem $(top_srcdir)/third_party/catch \
              -isystem $(top_srcdir)/third_party/clara \
              -I$(top_srcdir)/tests \
              -I$(top_srcdir)/src -I$(top_builddir)/src

AM_LDFLAGS = $(gcov_ldflags)

include_HEADERS = include/atmi.h include/xatmi.h \
                  include/fml32.h \
                  include/userlog.h \
                  include/atmidefs.h \
                  include/tmenv.h \
                  include/xa.h \
                  include/tx.h \
                  include/tpadm.h

lib_LTLIBRARIES = src/libfuxedo.la

noinst_HEADERS = src/compat.h

src_libfuxedo_la_SOURCES = src/xatmi.cpp src/mem.cpp \
                           src/fml32.cpp src/expr.cpp \
                           src/view32.cpp src/fv32.cpp \
                           src/server.cpp src/client.cpp src/ws.cpp src/xml.cpp \
                           src/mib.cpp src/ubb2mib.cpp src/cfg.cpp \
                           src/userlog.cpp \
                           src/ipc.cpp \
                           src/qmxa.cpp src/nonexa.cpp src/tx.cpp src/trx.cpp \
                           src/trace.cpp src/dmcfg.cpp src/dmlink.cpp \
                           src/misc.cpp src/base64.cpp \
                           src/tpadmcall.cpp src/tmq.cpp src/qm.cpp \
                           src/tlog.cpp src/events.cpp src/evsrv.cpp

src_libfuxedo_la_LDFLAGS = -lpthread

bin_PROGRAMS = src/mkfldhdr32 src/viewc32 src/viewdis32 src/ud32 \
               src/crdl src/crlog src/dmloadcf src/dmunloadcf src/dmadmin \
               src/fux \
               src/tmipcrm \
               src/tmadmin \
               src/qmadmin \
               src/tmloadcf src/tmunloadcf \
               src/buildserver src/buildclient src/buildtms \
               src/tmboot src/tmshutdown \
               src/BBL src/TMS src/TMQUEUE src/TMQFORWARD src/GWADM \
               src/GWTDOMAIN src/WSL src/TMUSREVT src/TMSYSEVT

src_qmadmin_SOURCES = src/qmadmin.cpp
src_qmadmin_LDADD = src/libfuxedo.la

src_buildtms_SOURCES = src/buildtms.cpp
src_buildserver_SOURCES = src/buildserver.cpp
src_buildclient_SOURCES = src/buildclient.cpp

src_tmboot_SOURCES = src/tmboot.cpp
src_tmboot_LDADD = src/libfuxedo.la

src_TMS_SOURCES = src/TMS.cpp
src_TMS_LDADD = src/libfuxedo.la

src_BBL_SOURCES = src/BBL.cpp
src_BBL_LDADD = src/libfuxedo.la -lpthread

src_dmloadcf_SOURCES = src/dmloadcf.cpp
src_dmloadcf_LDADD = src/libfuxedo.la

src_dmunloadcf_SOURCES = src/dmunloadcf.cpp
src_dmunloadcf_LDADD = src/libfuxedo.la

src_dmadmin_SOURCES = src/dmadmin.cpp
src_dmadmin_LDADD = src/libfuxedo.la

src_TMQUEUE_SOURCES = src/TMQUEUE.cpp
src_TMQUEUE_LDADD = src/libfuxedo.la

src_TMUSREVT_SOURCES = src/TMUSREVT.cpp
src_TMUSREVT_LDADD = src/libfuxedo.la

src_TMSYSEVT_SOURCES = src/TMSYSEVT.cpp
src_TMSYSEVT_LDADD = src/libfuxedo.la

src_TMQFORWARD_SOURCES = src/TMQFORWARD.cpp
src_TMQFORWARD_LDADD = src/libfuxedo.la -lpthread

src_GWADM_SOURCES = src/GWADM.cpp
src_GWADM_LDADD = src/libfuxedo.la

src_GWTDOMAIN_SOURCES = src/GWTDOMAIN.cpp
src_GWTDOMAIN_LDADD = src/libfuxedo.la

src_WSL_SOURCES = src/WSL.cpp
src_WSL_LDADD = src/libfuxedo.la

src_tmadmin_SOURCES = src/tmadmin.cpp
src_tmadmin_LDADD = src/libfuxedo.la

src_tmshutdown_SOURCES = src/tmshutdown.cpp
src_tmshutdown_LDADD = src/libfuxedo.la

src_mkfldhdr32_SOURCES = src/mkfldhdr32.cpp
src_mkfldhdr32_LDADD = src/libfuxedo.la

src_viewc32_SOURCES = src/viewc32.cpp
src_viewc32_LDADD = src/libfuxedo.la

src_viewdis32_SOURCES = src/viewdis32.cpp
src_viewdis32_LDADD = src/libfuxedo.la

src_ud32_SOURCES = src/ud32.cpp
src_ud32_LDADD = src/libfuxedo.la

src_crdl_SOURCES = src/crdl.cpp
src_crdl_LDADD = src/libfuxedo.la

src_crlog_SOURCES = src/crlog.cpp
src_crlog_LDADD = src/libfuxedo.la

src_tmloadcf_SOURCES = src/tmloadcf.cpp
src_tmloadcf_LDADD = src/libfuxedo.la

src_tmunloadcf_SOURCES = src/tmunloadcf.cpp
src_tmunloadcf_LDADD = src/libfuxedo.la

src_fux_SOURCES = src/fux.cpp
src_fux_LDADD = src/libfuxedo.la

src_tmipcrm_SOURCES = src/tmipcrm.cpp
src_tmipcrm_LDADD = src/libfuxedo.la

udataobjdir = @prefix@/udataobj
udataobj_DATA = RM include/tpadm

TESTS = tests/compat/xatmi tests/compat/fml32 tests/compat/expr tests/compat/view32 tests/compat/hardening tests/compat/xmlfml tests/impl/mib tests/impl/dmlink tests/impl/ipcq tests/compat/base64 tests/impl/userlog tests/impl/trx tests/impl/qm tests/impl/cfg tests/impl/tlog tests/impl/qmtx
check_PROGRAMS = $(TESTS)

AM_TESTS_ENVIRONMENT = FLDTBLDIR32=.:src:tests FIELDTBLS32=dummy,fields \
                       VIEWDIR32=tests/compat VIEWFILES32=view32test.V

BUILT_SOURCES = tests/compat/view32test.h tests/compat/view32test.V
CLEANFILES = tests/compat/view32test.h tests/compat/view32test.V

tests/compat/view32test.h tests/compat/view32test.V: $(top_srcdir)/tests/compat/view32test.v $(top_builddir)/src/viewc32$(EXEEXT)
	cd tests/compat && FLDTBLDIR32=$(abs_top_srcdir)/tests FIELDTBLS32=fields $(abs_top_builddir)/src/viewc32 $(abs_top_srcdir)/tests/compat/view32test.v

tests_impl_userlog_SOURCES = tests/impl/userlog.cpp tests/tests-main.cpp
tests_impl_userlog_LDADD = src/libfuxedo.la

tests_compat_base64_SOURCES = tests/compat/base64.cpp tests/tests-main.cpp
tests_compat_base64_LDADD = src/libfuxedo.la

tests_impl_ipcq_SOURCES = tests/impl/ipcq.cpp tests/tests-main.cpp
tests_impl_ipcq_LDADD = src/libfuxedo.la

tests_compat_xatmi_SOURCES = tests/compat/xatmi.cpp tests/tests-main.cpp
tests_compat_xatmi_LDADD = src/libfuxedo.la

tests_compat_fml32_SOURCES = tests/compat/fml32.cpp tests/tests-main.cpp
tests_compat_fml32_LDADD = src/libfuxedo.la

tests_compat_expr_SOURCES = tests/compat/expr.cpp tests/tests-main.cpp
tests_compat_expr_LDADD = src/libfuxedo.la

tests_compat_hardening_SOURCES = tests/compat/hardening.cpp tests/tests-main.cpp
tests_compat_hardening_LDADD = src/libfuxedo.la

tests_compat_view32_SOURCES = tests/compat/view32.cpp tests/tests-main.cpp

tests_compat_xmlfml_SOURCES = tests/compat/xmlfml.cpp tests/tests-main.cpp
tests_compat_xmlfml_LDADD = src/libfuxedo.la
tests_compat_view32_LDADD = src/libfuxedo.la
tests_compat_view32_CPPFLAGS = -I$(top_builddir)/tests/compat

tests_impl_mib_SOURCES = tests/impl/mib.cpp tests/tests-main.cpp
tests_impl_dmlink_SOURCES = tests/impl/dmlink.cpp tests/tests-main.cpp
tests_impl_dmlink_LDADD = src/libfuxedo.la
tests_impl_mib_LDADD = src/libfuxedo.la

tests_impl_trx_SOURCES = tests/impl/trx.cpp tests/tests-main.cpp
tests_impl_trx_LDADD = src/libfuxedo.la

tests_impl_qm_SOURCES = tests/impl/qm.cpp tests/tests-main.cpp

tests_impl_cfg_SOURCES = tests/impl/cfg.cpp tests/tests-main.cpp
tests_impl_cfg_LDADD = src/libfuxedo.la

tests_impl_tlog_SOURCES = tests/impl/tlog.cpp tests/tests-main.cpp
tests_impl_tlog_LDADD = src/libfuxedo.la
tests_impl_tlog_CPPFLAGS = -I$(top_srcdir)/src

tests_impl_qmtx_SOURCES = tests/impl/qmtx.cpp tests/tests-main.cpp
tests_impl_qmtx_LDADD = src/libfuxedo.la
tests_impl_qmtx_CPPFLAGS = -I$(top_srcdir)/src
tests_impl_qm_LDADD = src/libfuxedo.la

clang-format:
	clang-format -style=Google -i \
                            $(top_srcdir)/include/*.h \
                            $(top_srcdir)/src/*.cpp \
                            $(top_srcdir)/src/*.h \
                            $(top_srcdir)/tests/*.cpp \
                            $(top_srcdir)/install-tests/*/*.c

installcheck-local:
	TUXDIR=$(prefix) CXX="$(CXX)" CXXFLAGS="$(CXXFLAGS) $(AM_CXXFLAGS) $(AM_LDFLAGS)" make -C install-tests

install-exec-hook:
	(cd $(DESTDIR)${libdir}; $(LN_S) -f libfuxedo.so libtmib.so)
	(cd $(DESTDIR)${libdir}; $(LN_S) -f libfuxedo.so libtux.so)
	(cd $(DESTDIR)${libdir}; $(LN_S) -f libfuxedo.so libbuft.so)
	(cd $(DESTDIR)${libdir}; $(LN_S) -f libfuxedo.so libfml.so)
	(cd $(DESTDIR)${libdir}; $(LN_S) -f libfuxedo.so libfml32.so)
	(cd $(DESTDIR)${libdir}; $(LN_S) -f libfuxedo.so libengine.so)
	(cd $(DESTDIR)${libdir}; $(LN_S) -f libfuxedo.so libwsc.so)

uninstall-hook:
	(cd $(DESTDIR)${libdir}; rm -f libtmib.so)
	(cd $(DESTDIR)${libdir}; rm -f libtux.so)
	(cd $(DESTDIR)${libdir}; rm -f libbuft.so)
	(cd $(DESTDIR)${libdir}; rm -f libfml.so)
	(cd $(DESTDIR)${libdir}; rm -f libfml32.so)
	(cd $(DESTDIR)${libdir}; rm -f libengine.so)
	(cd $(DESTDIR)${libdir}; rm -f libwsc.so)
