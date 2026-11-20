AC_INIT([Fuxedo], [0.9.1], [aivars.kalvans@gmail.com])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_HEADERS([src/config.h])

# Oracle Tuxedo generation to emulate: 12.1 (default) or 22.1. Fixed per
# build; see src/compat.h and the compat-mode design note.
AC_ARG_WITH([tuxedo-compat],
  AS_HELP_STRING([--with-tuxedo-compat=VER],
    [Oracle Tuxedo behavior to emulate: 12.1 or 22.1 @<:@default=12.1@:>@]),
  [tuxedo_compat="$withval"],
  [tuxedo_compat=12.1])
case "$tuxedo_compat" in
  12.1) tuxedo_compat_num=121 ;;
  22.1) tuxedo_compat_num=221 ;;
  *) AC_MSG_ERROR([--with-tuxedo-compat must be 12.1 or 22.1]) ;;
esac
AC_DEFINE_UNQUOTED([FUXEDO_TUX_COMPAT], [$tuxedo_compat_num],
  [Oracle Tuxedo generation emulated by this build: 121 or 221])

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

# Optional USDT probes (build-time only). sys/sdt.h comes from
# systemtap-sdt-dev; without it the FUX_PROBE macros compile to nothing.
AC_CHECK_HEADERS([sys/sdt.h])

# Keep frame pointers so uprobe-based tracers can walk stacks; the ATMI
# boundary is IPC-bound, the register cost is not measurable here.
AM_CXXFLAGS="$AM_CXXFLAGS -fno-omit-frame-pointer"

# TLS for domain/workstation links
PKG_CHECK_MODULES([OPENSSL], [libssl libcrypto], [
  LIBS="$OPENSSL_LIBS $LIBS"
  AM_CXXFLAGS="$AM_CXXFLAGS $OPENSSL_CFLAGS"
], [
  AC_CHECK_LIB([ssl], [SSL_new], [],
    [AC_MSG_ERROR([OpenSSL not found; install the OpenSSL development package])])
  AC_CHECK_LIB([crypto], [EVP_PKEY_keygen_init], [],
    [AC_MSG_ERROR([libcrypto not found; install the OpenSSL development package])])
])

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
