AC_INIT([Fuxedo], [0.0.1], [aivars.kalvans@gmail.com])
AC_CONFIG_MACRO_DIRS([m4])

AM_INIT_AUTOMAKE([-Wall subdir-objects foreign nostdinc])
AM_SILENT_RULES([yes])

AC_PROG_CXX

AC_ARG_ENABLE(gcov,
AS_HELP_STRING([--enable-gcov],
               [enable gcovging, default: no]),
[case "${enableval}" in
             yes) gcov=true ;;
             no)  gcov=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-gcov]) ;;
esac],
[gcov=false])
AM_CONDITIONAL(GCOV, test x"$gcov" = x"true")

m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
LT_INIT([shared])
AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_17(ext, mandatory)

AC_SUBST([AM_CXXFLAGS])
AC_SUBST([AM_LDFLAGS])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
