# SYNOPSIS
#
#   AX_CXX_HAVE_FILESYSTEM()
#
# DESCRIPTION
#
#   This macro checks if std::filesystem or std::experimental::filesystem are
#   defined in the <filesystem> or <experimental/filesystem> headers.
#
#   If it is, define the ax_cv_cxx_have_filesystem environment variable to
#   "yes" and define HAVE_CXX_FILESYSTEM.
#
# LICENSE
#
#   Copyright (c) 2017 Julian Cromarty <julian.cromarty@gmail.com>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.  This file is offered as-is, without any
#   warranty.

#serial 3

AC_DEFUN([AX_CXX_HAVE_FILESYSTEM],
  [AC_CACHE_CHECK(
    [for std::filesystem::path in filesystem],
    ax_cv_cxx_have_filesystem,
    [dnl
      AC_LANG_PUSH([C++])
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
        [
          [#include <filesystem>]
          [using std::filesystem::path;]
        ],
        []
        )],
        [ax_cv_cxx_have_filesystem=yes],
        [ax_cv_cxx_have_filesystem=no]
      )
    AC_LANG_POP([C++])])
    if test x"$ax_cv_cxx_have_filesystem" = "xyes"
    then
      AC_DEFINE(HAVE_CXX_FILESYSTEM,
        1,
        [Define if filesystem defines the std::filesystem::path class.])
    fi
  AC_CACHE_CHECK(
    [for std::experimental::filesystem::path in experimental/filesystem],
    ax_cv_cxx_have_experimental_filesystem,
    [dnl
      AC_LANG_PUSH([C++])
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
        [
          [#include <experimental/filesystem>]
          [using std::experimental::filesystem::path;]
        ],
        []
        )],
        [ax_cv_cxx_have_experimental_filesystem=yes],
        [ax_cv_cxx_have_experimental_filesystem=no]
      )
    AC_LANG_POP([C++])])
    if test x"$ax_cv_cxx_have_experimental_filesystem" = "xyes"
    then
      AC_DEFINE(HAVE_CXX_EXPERIMENTAL_FILESYSTEM,
        1,
        [Define if filesystem defines the std::experimental::filesystem::path class.])
    fi
  ])
