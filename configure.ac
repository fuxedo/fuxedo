AC_INIT([Fuxedo], [0.0.1], [aivars.kalvans@gmail.com])
AC_CONFIG_MACRO_DIRS([m4])

AM_INIT_AUTOMAKE([-Wall subdir-objects foreign nostdinc])
AM_SILENT_RULES([yes])

AC_PROG_CXX

AC_ARG_ENABLE([code-coverage],
  AS_HELP_STRING([--enable-code-coverage],
  [Whether to enable code coverage support]),,
  enable_code_coverage=no)
AM_CONDITIONAL([CODE_COVERAGE_ENABLED], [test "x$enable_code_coverage" = xyes])

m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
LT_INIT([shared])
AC_LANG([C++])
AC_PROG_LN_S
AX_CXX_COMPILE_STDCXX_17(ext, mandatory)
AX_CXX_HAVE_FILESYSTEM

# g++-8.3 compiles the code but core dumps at runtime unless linked with -lstdc++fs
AC_CACHE_CHECK(
  [if -lstdc++fs is needed for filesystem],
  ax_cv_cxx_have_libfilesystem,
  [dnl
    ac_save_CXX="$CXX"
    CXX="$CXX -lstdc++fs"
    AC_LINK_IFELSE([AC_LANG_PROGRAM(
      [
        [#ifdef HAVE_CXX_FILESYSTEM]
        [#include <filesystem>]
        [using std::filesystem::path;]
        [#else]
        [#include <experimental/filesystem>]
        [using std::experimental::filesystem::path;]
        [#endif]
      ],
      []
      )],
      [LIBS="-lstdc++fs $LIBS"]
      [ax_cv_cxx_have_libfilesystem=yes],
      [ax_cv_cxx_have_libfilesystem=no]
    )
    CXX="$ac_save_CXX"
  ])
 
AC_SUBST([AM_CXXFLAGS])
AC_SUBST([AM_LDFLAGS])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
