AC_INIT([Fuxedo], [0.1], [aivars.kalvans@gmail.com], [fuxedo], [http://phi.lv/])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])

AC_CONFIG_MACRO_DIR([m4])
m4_include([m4/ax_boost_base.m4])
m4_include([m4/ax_boost_thread.m4])
m4_include([m4/ax_boost_unit_test_framework.m4])
m4_include([m4/ax_cxx_compile_stdcxx_11.m4])

AC_PROG_CXX
AC_PROG_LIBTOOL

AC_LANG_PUSH([C++])

AX_BOOST_BASE([1.39.0])
AX_BOOST_THREAD
AX_BOOST_UNIT_TEST_FRAMEWORK
AX_CXX_COMPILE_STDCXX_11([noext])

AC_LANG_POP([C++])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile src/Makefile tests/Makefile])
AC_OUTPUT
