dnl Checks whether we need to pass -std=libc++ to CXXFLAGS. Sadly this is needed on OS X \
dnl which for some insane reason defaults to an ancient stdlibc++ :(
dnl To check for this, we try to use std::forward from <utility>

dnl Copyright (c) 2015-2016 Tim Kosse <tim.kosse@filezilla-project.org>

dnl Copying and distribution of this file, with or without modification, are
dnl permitted in any medium without royalty provided the copyright notice
dnl and this notice are preserved. This file is offered as-is, without any
dnl warranty.

AC_DEFUN([CHECK_REGEX], [

  AC_LANG_PUSH(C++)

  AC_MSG_CHECKING([for whether we need to use Boost Regex due GCC bug 86164])

  AC_COMPILE_IFELSE([
    AC_LANG_PROGRAM([[
      #include <regex>
    ]], [[
      #if !defined(_MSC_VER) && !defined(_LIBCPP_VERSION)
        #error Cannot use libstdc++ for regex due to GCC bug 86164
      #endif
      return 0;
    ]])
  ], [
    AC_MSG_RESULT([no])
  ], [
    AC_MSG_RESULT([yes])
    AC_MSG_CHECKING([for Boost Regex])
    AC_COMPILE_IFELSE([
      AC_LANG_PROGRAM([[
        #define BOOST_REGEX_STANDALONE
        #include <boost/regex.hpp>
      ]], [[
        static_assert(BOOST_RE_VERSION >= 500, "Too old boost regex");
        return 0;
      ]])
    ], [
      AC_MSG_RESULT([yes])
      AC_DEFINE([USE_BOOST_REGEX], [1], [GCC bug 86164 requires use of Boost Regex insteadd])
    ], [
      AC_MSG_FAILURE([Boost Regex 1.76 or higher not found.])
    ])
  ])

  AC_LANG_POP(C++)
])
