AC_INIT([FileZilla],[3.70.6],[tim.kosse@filezilla-project.org],[],[https://filezilla-project.org/])

ENGINE_VERSION="$PACKAGE_VERSION"

AH_TOP([
#ifndef FILEZILLA_CONFIG_HEADER
#define FILEZILLA_CONFIG_HEADER
])
AH_BOTTOM([
#endif
])
AC_CONFIG_HEADERS([config/config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR(config)
AM_INIT_AUTOMAKE([dist-xz no-dist-gzip nostdinc])

AC_CANONICAL_HOST

AC_PROG_CXX
AC_PROG_OBJCXX
AM_PROG_CC_C_O
AC_PROG_INSTALL

LT_INIT([disable-static])
LT_LANG([C++])

AC_CACHE_SAVE

AC_ARG_ENABLE(locales, AS_HELP_STRING([--enable-locales],[Build message catalogs]), \
        [locales="$enableval"], [locales="yes"])

# Todo: Investigate using sub-configure scripts
AC_ARG_ENABLE(localesonly, AS_HELP_STRING([--enable-localesonly],[Just build locales]), \
        [localesonly="$enableval"], [localesonly="no"])

if test "$locales" != "yes" -a "$localesonly" = "yes"; then
  AC_MSG_ERROR([invalid flags: --disable-locales cannot be used together with --enable-localesonly])
fi

if test "$localesonly" = "yes"; then
  buildmain="no"
else
  buildmain="yes"
fi

# Check if we're on Windows
if echo $host_os | grep 'cygwin\|mingw\|^msys$' > /dev/null 2>&1; then
  windows=1
else
  windows=0
fi

# Check if we're on Mac
if test "$host_vendor" = "apple"; then
  mac=1
else
  mac=0
fi

# If neither, we're Unix(-like)
if test "$mac" = "0" && test "$windows" = "0"; then
  unix=1
else
  unix=0
fi

if ! test "$localesonly" = "yes"; then

  if test "X$GCC" = Xyes; then
    AX_APPEND_FLAG(-Wall, CFLAGS)
    AX_APPEND_FLAG(-g, CFLAGS)
    AX_APPEND_FLAG(-Wall, CXXFLAGS)
    AX_APPEND_FLAG(-g, CXXFLAGS)
  fi

  # Do this early: Compiler and linker flags to work around a nasty bug in Xcode.
  AX_CHECK_COMPILE_FLAG([-Werror=partial-availability], [
    # This warning should be on by default yet it's not even enabled by -Wall. Apple screwed up big time.
    AX_APPEND_FLAG(-Werror=partial-availability, CFLAGS)
    AX_APPEND_FLAG(-Werror=partial-availability, CXXFLAGS)
  ])

  AX_CHECK_LINK_FLAG([-Wl,-no_weak_imports], [
    # If the programmer wants weak symbols he would just used dlopen/dlsym
    # But not so on Apple systems where even basic functions like clock_gettime can
    # be randomly missing at runtime.
    AX_APPEND_FLAG([-Wl,-no_weak_imports], LFDLAGS)
  ])

  # Check for C++17 support
  AX_CXX_COMPILE_STDCXX_17([noext],[mandatory])
  CHECK_LIBCXX
  AX_APPEND_FLAG([-std=c++17], [OBJCXXFLAGS])

  # libstdc++ as shipped with GCC 4.7 lacks std::map::emplace
  FZ_CHECK_MAP_EMPLACE

  # libstdc++ as shipped with GCC 4.7 has a steady clock that isn't steady (who implements such crap?)
  CHECK_STEADY_CLOCK

  # No thread_local in GCC 4.7 and it for some reason isn't supported on OS X either
  CHECK_THREAD_LOCAL

  # std::atomic on PPC seems to require -latomic
  CHECK_ATOMIC

  # GCC bug 86164
  CHECK_REGEX

  # Add build information to config.h
  # ---------------------------------

  # Add host to config.h
  AC_DEFINE_UNQUOTED(USED_HOST, ["$host"], [Host system under which the program will run.])
  AC_DEFINE_UNQUOTED(USED_BUILD, ["$build"], [Build system under which the program was compiled on.])

  # Add used CXXFLAGS to config.h
  AC_DEFINE_UNQUOTED(USED_CXXFLAGS, ["$CXXFLAGS"], [Define to the used CXXFLAGS to compile this package.])

  # Get compiler (version)
  AH_TEMPLATE(USED_COMPILER, [Define to name and version of used compiler])
  if COMPILER=`$CC --version | head -n 1`; then
    AC_DEFINE_UNQUOTED(USED_COMPILER, ["$COMPILER"])
  fi

  # Get buildtype.
  AC_ARG_ENABLE(buildtype, AS_HELP_STRING([--enable-buildtype=TYPE],[DO NOT USE. Reserved for use by official maintainer. TYPE can either be 'official' or 'nightly'.]),
    [buildtype="$enableval"], [buildtype=])
  buildtype_valid=false
  if test -z "$buildtype"; then
    buildtype_valid=true
  elif test "$buildtype" = "official"; then
    buildtype_valid=true
  elif test "$buildtype" = "nightly"; then
    buildtype_valid=true
  fi

  if test "$buildtype_valid" != "true"; then
    AC_MSG_ERROR([invalid buildtype passed])
  fi

  if ! test -z "$buildtype"; then
    AC_DEFINE_UNQUOTED(BUILDTYPE, ["$buildtype"], [Buildtype, indicates official builds and nightly builds])
  fi
fi

# Shell extension
# ---------------

AC_MSG_NOTICE([Running sub-configure for shell extension])
if false; then
  # For autoreconf machinery
  AC_CONFIG_SUBDIRS([src/fzshellext])
fi

OLD_DIR="`pwd`"
mkdir -p "$OLD_DIR/src/fzshellext/32"
mkdir -p "$OLD_DIR/src/fzshellext/64"

AC_ARG_ENABLE(shellext, [], \
        [shellext="$enableval"], [shellext="yes"])

if test "$localesonly" != "yes" && test "$windows" = "1" && test "$shellext" = "yes"; then
  cd "$OLD_DIR/src/fzshellext/32" || exit 1
  "$ac_abs_confdir/src/fzshellext/configure" --prefix="$prefix" --exec-prefix="$exec_prefix" --host=i686-w64-mingw32 || exit 1
  cd "$OLD_DIR/src/fzshellext/64" || exit 1
  "$ac_abs_confdir/src/fzshellext/configure" --prefix="$prefix" --exec-prefix="$exec_prefix" --host=x86_64-w64-mingw32 || exit 1
else
  cd "$OLD_DIR/src/fzshellext/64" || exit 1
  "$ac_abs_confdir/src/fzshellext/configure" --disable-shellext || exit 1
fi
cd "$OLD_DIR"



if test "$buildmain" = "yes"; then

  # GUI or not
  # ----------
  AC_ARG_ENABLE(gui, AS_HELP_STRING([--enable-gui@<:@=ARG@:>@],[Enable the GUI client. Default: yes]), [], [enable_gui="yes"])

  # Require pkg-config
  # ------------------
  PKG_PROG_PKG_CONFIG
  if test -z "$PKG_CONFIG"; then
    AC_MSG_ERROR([pkg-config not found. pkg-config is required to check for some dependencies.])
  fi

  # Find libfilezilla
  # -----------------
  PKG_CHECK_MODULES(LIBFILEZILLA, [libfilezilla >= 0.56.1],, [
	AC_MSG_ERROR([libfilezilla not found: $LIBFILEZILLA_PKG_ERRORS. You can download it from https://lib.filezilla-project.org/])
  ])
  AC_SUBST(LIBFILEZILLA_CFLAGS)
  AC_SUBST(LIBFILEZILLA_LIBS)

  # Find fzssh
  # -----------------
  PKG_CHECK_MODULES(LIBFZSSH, [libfzssh-client >= 1.3.0],, [
	AC_MSG_ERROR([libfzssh-client not found: $LIBFZSSH_PKG_ERRORS. You can download it from https://fzssh.filezilla-project.org/])
  ])
  AC_SUBST(LIBFZSSH_CFLAGS)
  AC_SUBST(LIBFZSSH_LIBS)

  # Detect wxWidgets
  # ----------------

  if test "$enable_gui" = "yes"; then

    AM_OPTIONS_WXCONFIG

    MIN_WX_VERSION="3.2.1" # Do not downgrade this. wx 3.2.0 and older have too many known and fixed bugs affecting FileZilla.
    AM_PATH_WXCONFIG([$MIN_WX_VERSION], [wxWin=1], , [aui,xrc,adv,core,xml,base], [--unicode=yes --universal=no])
    if test "$wxWin" != 1; then
      AC_MSG_ERROR([
        wxWidgets must be installed on your system
        but either the wx-config script couldn't be found or
        no compatible wxWidgets configuration has been installed.

        Compatible wxWidgets configurations are the unicode builds
        of wxGTK, wxMac and wxMSW.

        Please check that wx-config is in path, the directory
        where wxWidgets libraries are installed (returned by
        'wx-config --libs' command) is in LD_LIBRARY_PATH or
        equivalent variable and wxWidgets version is $MIN_WX_VERSION or above.
      ])
    fi
    if test "${WX_VERSION_MAJOR}.${WX_VERSION_MINOR}" = "3.3"; then
      AC_MSG_ERROR([You must use wxWidgets 3.2.x, development versions of wxWidgets are not supported.])
    fi

    # --universal=no doesn't work correctly, it still accepts universal builds. Reject it manually here.
    if echo "$WX_CPPFLAGS" | grep __WXUNIVERSAL__ > /dev/null; then
      AC_MSG_ERROR([wxUniversal is not acceptable as flavour of wxWidgets. Please recompile wxWidgets with --disable-universal])
    fi
    if echo "$WX_CPPFLAGS" | grep __WXMOTIF__ > /dev/null; then
      AC_MSG_ERROR([wxMotif is not acceptable as flavour of wxWidgets. Please recompile wxWidgets with the GTK flavour])
    fi

    AC_LANG_PUSH(C++)
    CPPFLAGS_OLD="$CPPFLAGS"
    CXXFLAGS_OLD="$CXXFLAGS"
    CPPFLAGS="$CPPFLAGS $WX_CPPFLAGS"
    CXXFLAGS="$CXXFLAGS $WX_CXXFLAGS_ONLY"

    AC_MSG_CHECKING([positional printf argument support in wxWidgets])
    AC_COMPILE_IFELSE(
      [AC_LANG_PROGRAM([[
          #include <wx/wx.h>
          #if !wxUSE_PRINTF_POS_PARAMS
            #error wx compiled without wxUSE_PRINTF_POS_PARAMS
          #endif
        ]])],
        AC_MSG_RESULT([yes]),
        [
          AC_MSG_RESULT([no])
          AC_MSG_FAILURE([Positional printf argument support in wxWidgets is required by FileZilla. Please rebuild wxWidgets and pass --enable-printfposparam to its configure script.])
        ]
      )

    AC_MSG_CHECKING([GUI support in wxWidgets])
    AC_COMPILE_IFELSE(
      [AC_LANG_PROGRAM([[
          #include <wx/wx.h>
          #if !wxUSE_GUI
            #error wx compiled without wxUSE_GUI
          #endif
        ]])],
        AC_MSG_RESULT([yes]),
        [
          AC_MSG_RESULT([no])
          AC_MSG_FAILURE([GUI support in wxWidgets is required by FileZilla. Please rebuild wxWidgets and pass --enable-gui to its configure script.])
        ]
      )

    CPPFLAGS="$CPPFLAGS_OLD"
    CXXFLAGS="$CXXFLAGS_OLD"
    AC_LANG_POP

    AC_SUBST(WX_CONFIG_WITH_ARGS)

    AC_CACHE_SAVE
  fi

  if test "$windows" = "1"; then
    FZ_CHECK_ICOPYHOOKW
  fi

  # Check for windres on MinGW builds
  # ---------------------------------

  if test "$windows" = "1"; then
    AC_CHECK_TOOL([WINDRES], [windres])
    if test "x$WINDRES" = "x"; then
      AC_MSG_ERROR([windres could not be found, please make sure this program is within your path.])
    fi
    use_resourcefile=true
  fi
  AC_SUBST(WINDRES)

  # Set version macros
  # ------------------

  # version.rc.in needs a major, minor and micro version number
  vnumber=${PACKAGE_VERSION%-*}
  PACKAGE_VERSION_MAJOR=${vnumber%%.*}
  vnumber=${vnumber#$PACKAGE_VERSION_MAJOR}
  vnumber=${vnumber#.}
  PACKAGE_VERSION_MINOR=${vnumber%%.*}
  vnumber=${vnumber#$PACKAGE_VERSION_MINOR}
  vnumber=${vnumber#.}
  PACKAGE_VERSION_MICRO=${vnumber%%.*}
  vnumber=${vnumber#$PACKAGE_VERSION_MICRO}
  vnumber=${vnumber#.}
  PACKAGE_VERSION_NANO=${vnumber%%.*}

  if test -z "$PACKAGE_VERSION_MAJOR"; then
    PACKAGE_VERSION_MAJOR=0
  fi
  if test -z "$PACKAGE_VERSION_MINOR"; then
    PACKAGE_VERSION_MINOR=0
  fi
  if test -z "$PACKAGE_VERSION_MICRO"; then
    PACKAGE_VERSION_MICRO=0
  fi
  if test -z "$PACKAGE_VERSION_NANO"; then
    PACKAGE_VERSION_NANO=0
  fi

  PACKAGE_VERSION_SHORT=${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}.${PACKAGE_VERSION_MICRO}
  AC_SUBST(PACKAGE_VERSION_SHORT)
  AC_DEFINE_UNQUOTED([PACKAGE_VERSION_SHORT], ["$PACKAGE_VERSION_SHORT"], [Shortened package version number])

  AC_SUBST(PACKAGE_VERSION_MAJOR)
  AC_SUBST(PACKAGE_VERSION_MINOR)
  AC_SUBST(PACKAGE_VERSION_MICRO)
  AC_SUBST(PACKAGE_VERSION_NANO)

  # Do the same for the engine version
  vnumber=${ENGINE_VERSION%-*}
  ENGINE_VERSION_MAJOR=${vnumber%%.*}
  vnumber=${vnumber#$ENGINE_VERSION_MAJOR}
  vnumber=${vnumber#.}
  ENGINE_VERSION_MINOR=${vnumber%%.*}
  vnumber=${vnumber#$ENGINE_VERSION_MINOR}
  vnumber=${vnumber#.}
  ENGINE_VERSION_MICRO=${vnumber%%.*}
  vnumber=${vnumber#$ENGINE_VERSION_MICRO}
  vnumber=${vnumber#.}
  ENGINE_VERSION_NANO=${vnumber%%.*}

  if test -z "$ENGINE_VERSION_MAJOR"; then
    ENGINE_VERSION_MAJOR=0
  fi
  if test -z "$ENGINE_VERSION_MINOR"; then
    ENGINE_VERSION_MINOR=0
  fi
  if test -z "$ENGINE_VERSION_MICRO"; then
    ENGINE_VERSION_MICRO=0
  fi
  if test -z "$ENGINE_VERSION_NANO"; then
    ENGINE_VERSION_NANO=0
  fi

  ENGINE_VERSION_SHORT=${ENGINE_VERSION_MAJOR}.${ENGINE_VERSION_MINOR}.${ENGINE_VERSION_MICRO}
  AC_SUBST(ENGINE_VERSION_SHORT)
  AC_DEFINE_UNQUOTED([ENGINE_VERSION_SHORT], ["$ENGINE_VERSION_SHORT"], [Shortened engine version number])

  AC_SUBST(ENGINE_VERSION_MAJOR)
  AC_SUBST(ENGINE_VERSION_MINOR)
  AC_SUBST(ENGINE_VERSION_MICRO)
  AC_SUBST(ENGINE_VERSION_NANO)

  AC_DEFINE_UNQUOTED([ENGINE_VERSION], ["$ENGINE_VERSION"], [Engine version number])
  AC_SUBST(ENGINE_VERSION)

  # Enable DEP and ASLR linker flags
  # --------------------------------

  AX_CHECK_LINK_FLAG([-Wl,--dynamicbase], [AX_APPEND_FLAG([-Wl,--dynamicbase], LDFLAGS)])
  AX_CHECK_LINK_FLAG([-Wl,--nxcompat], [AX_APPEND_FLAG([-Wl,--nxcompat], LDFLAGS)])

  # Set Windows-specific linker flags
  # ---------------------------------
  if test "$windows" = "1"; then
    # We currently require 7 minimum.
    AX_CHECK_LINK_FLAG([-Wl,--major-subsystem-version -Wl,6], [AX_APPEND_FLAG([-Wl,--major-subsystem-version -Wl,6], LDFLAGS)])
    AX_CHECK_LINK_FLAG([-Wl,--minor-subsystem-version -Wl,1], [AX_APPEND_FLAG([-Wl,--minor-subsystem-version -Wl,1], LDFLAGS)])
    AX_CHECK_LINK_FLAG([-Wl,--major-os-version -Wl,6], [AX_APPEND_FLAG([-Wl,--major-os-version -Wl,6], LDFLAGS)])
    AX_CHECK_LINK_FLAG([-Wl,--minor-os-version -Wl,1], [AX_APPEND_FLAG([-Wl,--minor-os-version -Wl,1], LDFLAGS)])

    # Set the Large Address Aware flag on 32bit Windows
    if ! echo $host_cpu | grep "x86_64" > /dev/null 2>&1; then
      AX_CHECK_LINK_FLAG([-Wl,--large-address-aware], [AX_APPEND_FLAG([-Wl,--large-address-aware], LDFLAGS)])
    else
      # Base address >4GB to disable some compatibility mode the kernel uses for old programs
      AX_CHECK_LINK_FLAG([-Wl,--image-base -Wl,0x120000000], [AX_APPEND_FLAG([-Wl,--image-base -Wl,0x120000000], LDFLAGS)])
    fi
  fi

  # Mac OS X specific checks
  # ------------------------

  # Create application bundle on OS X
  if test "$mac" = "1"; then
    macappbundle="yes"
  else
    macappbundle="no"
  fi

  AC_ARG_ENABLE(mac-sandbox, AS_HELP_STRING([--enable-mac-sandbox],[Enables use of OS X's App Sandbox.]),
    [use_mac_sandbox="$enableval"], [use_mac_sandbox="no"])
  if test "$use_mac_sandbox" = "yes"; then
    AC_DEFINE([USE_MAC_SANDBOX], [1],
       [Define to 1 if the App Sandbox on OS X should be used.])
    CFBUNDLEIDSUFFIX=".sandbox"
  else
    CFBUNDLEIDSUFFIX=""
  fi
  AC_SUBST(CFBUNDLEIDSUFFIX)

  # Installer creation under Windows
  # --------------------------------

  # Create installer under Windows
  if test "$windows" = "1"; then
    makensisscript="yes"
    if echo $host_cpu | grep "x86_64" > /dev/null 2>&1; then
      NSIS_64BIT=1
    else
      NSIS_64BIT=0
    fi
    AC_SUBST(NSIS_64BIT)
  else
    makensisscript="no"
  fi

  if test "$windows" = "1"; then
    AC_CONFIG_FILES(data/install.nsi)
    AC_CONFIG_FILES(data/makezip.sh)
  fi

  # Libidn checks
  # -------------

  IDN_LIB=
  if test "$windows" != "1"; then
    AC_LANG_PUSH(C++)
    AX_CHECK_DEFINE([netdb.h], [AI_IDN],
      [],
      [
        AC_CHECK_HEADER(idna.h,,
        [
          AC_MSG_ERROR([idna.h not found which is part of GNU libidn.])
        ])

        AC_ARG_WITH(idn-lib,
            AS_HELP_STRING([--with-idn-lib=FILE],[Use the given path to the idn library.]),
        [
          if test "$withval" != "yes" -a "$withval" != ""; then
              IDN_LIB=$withval
          fi
        ])

        if test "x$IDN_LIB" = "x"; then
          AC_CHECK_LIB(idn, stringprep_check_version, IDN_LIB="-lidn",
                       AC_MSG_ERROR([GNU libidn not found. Try using --with-idn-lib=FILE to specify the library path.])
                       )
        fi
      ])
    AC_LANG_POP(C++)
  fi
  AC_SUBST(IDN_LIB)

  # Precompiled headers support
  # ---------------------------

  AC_MSG_CHECKING([whether to use precompiled headers])
  use_precomp=""
  AC_ARG_ENABLE(precomp, AS_HELP_STRING([--enable-precomp],[Use precompiled headers]), \
                use_precomp="$enableval")
  if test "x$enable_dependency_tracking" = "xno"; then
    use_precomp="no"
  fi
  if test -z "$use_precomp"; then
    if test "X$GCC" = Xyes; then
      if gcc_version=`$CC -dumpversion` > /dev/null 2>&1; then
        major=`echo $gcc_version | cut -d. -f1`
        minor=`echo $gcc_version | sed "s/@<:@-,a-z,A-Z@:>@.*//" | cut -d. -f2`
        if test -z "$major" || test -z "$minor"; then
          use_precomp=no
        elif test "$major" -ge 4; then
          use_precomp=yes
        else
          use_precomp=no
        fi
      else
        use_precomp=no
      fi
    else
      use_precomp=no
    fi
  fi
  if test "x$use_precomp" = "xyes"; then
    CPPFLAGS="$CPPFLAGS -fpch-preprocess"
    AC_MSG_RESULT([yes])
  else
    AC_MSG_RESULT([no])
  fi

  # CppUnit tests
  # -------------

  PKG_CHECK_MODULES(CPPUNIT, [cppunit >= 1.13.0], [has_cppunit="yes"], [:])

  # Updatecheck related flags
  # -------------------------

  AC_ARG_ENABLE(manualupdatecheck, AS_HELP_STRING([--disable-manualupdatecheck],[Disallow the user to manually check for new versions. Implies --disable-autoupdatecheck]),
    [manualupdatecheck="$enableval"], [manualupdatecheck="yes"])

  if test "$manualupdatecheck" = "no"; then
    manualupdatecheck="0"
  else
    manualupdatecheck="1"
  fi
  AC_DEFINE_UNQUOTED(FZ_MANUALUPDATECHECK, $manualupdatecheck, [Set to 1 to enable user initiated update checks])

  AC_ARG_ENABLE(autoupdatecheck, AS_HELP_STRING([--disable-autoupdatecheck],[Disables the ability of FileZilla to check for new versions automatically.]),
    [autoupdatecheck="$enableval"], [autoupdatecheck="yes"])

  if test "$manualupdatecheck" = "1" -a "$autoupdatecheck" != "no"; then
    autoupdatecheck="1"
  else
    autoupdatecheck="0"
  fi
  AC_DEFINE_UNQUOTED(FZ_AUTOUPDATECHECK, $autoupdatecheck, [Set to 1 to add support for automated update checks])

  # pugixml
  # ------

  FZ_CHECK_PUGIXML()

  # Libdbus
  # -------

  if test "$enable_gui" = "yes"; then
    AC_ARG_WITH(dbus, AS_HELP_STRING([--with-dbus],[Enable D-Bus support through libdbus. Used for GNOME Session manager D-Bus API. Default: auto]),
      [
     ],
       [
        with_dbus="auto"
      ])

    if test "$with_dbus" = "auto"; then
      if test "$mac" = 1; then
        with_dbus="no"
      elif test "$windows" = 1; then
        with_dbus="no"
      else
        with_dbus="yes"
      fi
    elif test "$with_dbus" = "yes"; then
      if test "$mac" = 1; then
        AC_MSG_ERROR([D-Bus support is not needed or available on your platform])
      elif test "$windows" = 1; then
        AC_MSG_ERROR([D-Bus support is not needed or available on your platform])
      fi
    else
      with_dbus="no"
    fi

    if test "$with_dbus" != "no"; then
      PKG_CHECK_MODULES(LIBDBUS, dbus-1 >= 1.2, [with_dbus_v="2"],
        [
          PKG_CHECK_MODULES(LIBDBUS, dbus-1, [with_dbus_v="1"],
            [
              AC_MSG_ERROR([libdbus not found. Install libdbus or configure with --without-dbus])
            ])
        ])
      AC_DEFINE_UNQUOTED(WITH_LIBDBUS, [$with_dbus_v], [Set to 2 to if libdbus >= 1.2 is available, set to 1 if an older version is available.])
    fi

    AC_MSG_CHECKING([D-Bus support])
    AC_MSG_RESULT([$with_dbus])

    # We want xdg-open on *nix
    if echo "`$WX_CONFIG_WITH_ARGS --basename`" | grep -i gtk > /dev/null 2>&1; then
      AC_PATH_PROG(xdgopen, xdg-open)
      if test -z "$xdgopen"; then
        AC_MSG_ERROR([xdg-open not found. This program is the preferred way to launch the default browser. Please install the xdg-utils.])
      fi

      if echo "`$WX_CONFIG_WITH_ARGS --basename`" | grep -i gtk2 > /dev/null 2>&1; then
        PKG_CHECK_MODULES(LIBGTK, gtk+-2.0,, [
          AC_MSG_ERROR([gtk+-2.0 was not found, even though the used version of wxWidgets depends on it. Are you missing the gtk+2.0 development files?])
        ])
      fi
      if echo "`$WX_CONFIG_WITH_ARGS --basename`" | grep -i gtk3 > /dev/null 2>&1; then
        PKG_CHECK_MODULES(LIBGTK, gtk+-3.0,, [
          AC_MSG_ERROR([gtk+-3.0 was not found, even though the used version of wxWidgets depends on it. Are you missing the gtk+3.0 development files?])
        ])
      fi

      AC_SUBST(LIBGTK_LIBS)
      AC_SUBST(LIBGTK_CFLAGS)
    fi
  else
    with_dbus=no
  fi

  # Some more obscure platforms (e.g. HP-UX) don't have setenv which is required by POSIX
  AC_CHECK_FUNC(setenv, [AC_DEFINE([HAVE_SETENV], [1], [Define to 1 if setenv function is available.])], [])
  AC_CHECK_FUNC(putenv, [AC_DEFINE([HAVE_PUTENV], [1], [Define to 1 if putenv function is available.])], [])

  # Some platforms, e.g. OS X, lack posix_fadvise
  AC_CHECK_FUNCS(posix_fadvise)

  CHECK_THREADSAFE_LOCALTIME
  CHECK_THREADSAFE_GMTIME
  CHECK_INVERSE_GMTIME

  # Must be done after setting linker flags
  CHECK_CLOCK_GETTIME

  # SQLite3
  # -------

  PKG_CHECK_MODULES(LIBSQLITE3, sqlite3 >= 3.7,, [

    AC_CHECK_HEADER(sqlite3.h,,
    [
      AC_MSG_ERROR([sqlite3.h not found which is part of SQLite3.])
    ])

    AC_CHECK_LIB(sqlite3, sqlite3_version, LIBSQLITE3_LIBS="-lsqlite3",
    [
      AC_MSG_ERROR([SQLite3 not found.])
    ])
  ])

  AC_SUBST(LIBSQLITE3_LIBS)
  AC_SUBST(LIBSQLITE3_CFLAGS)

  # Protocol configuration
  AC_ARG_ENABLE(ftp, AS_HELP_STRING([--enable-ftp@<:@=ARG@:>@],[Enable support for FTP(S). Default: yes]),
    [
    ],
    [
      enable_ftp="yes"
    ])
  if test "$enable_ftp" != "no"; then
    AC_DEFINE([ENABLE_FTP], [1], [Define if building with FTP(S) support.])
  fi

  AC_ARG_ENABLE(sftp, AS_HELP_STRING([--enable-sftp@<:@=ARG@:>@],[Enable support for SFTP. Default: yes]),
    [
    ],
    [
      enable_sftp="yes"
    ])
  if test "$enable_sftp" != "no"; then
    AC_DEFINE([ENABLE_SFTP], [1], [Define if building with SFTP support.])
  fi

  AC_ARG_ENABLE(storj, AS_HELP_STRING([--enable-storj@<:@=ARG@:>@],[Enable support for the Storj decentralized cloud storage provider. Default: no]),
    [
    ],
    [
      enable_storj="no"
    ])

  # Find libuplink
  # --------------

  if test "$enable_storj" != "no"; then
    PKG_CHECK_MODULES(LIBUPLINK, [libuplink],[
      AC_DEFINE([ENABLE_STORJ], [1], [Define if building with Storj support.])
      enable_storj="yes"
    ], [
      if test "$enable_storj" = "yes"; then
  	AC_MSG_ERROR([libuplink not found: $LIBUPLINKC_PKG_ERRORS. You can download it from https://github.com/storj/uplink-c])
      else
        enable_storj="no"
      fi
    ])
    AC_SUBST(LIBUPLINK_CFLAGS)
    AC_SUBST(LIBUPLINK_LIBS)
  fi
fi

# Everything translation related
# ------------------------------

FILEZILLA_LINGUAS=
FILEZILLA_LINGUAS_PO=
FILEZILLA_LINGUAS_MO=
FILEZILLA_LINGUAS_PO_NEW=

if test "$locales" = "yes"; then
  AC_PATH_PROG(xgettext, xgettext)
  if test -z "$xgettext"; then
    AC_MSG_ERROR([xgettext not found, please install the GNU gettext package and make sure xgettext is in the path.])
  fi

  AC_PATH_PROG(msgfmt, msgfmt)
  if test -z "$msgfmt"; then
    AC_MSG_ERROR([ msgfmt not found, please install the GNU gettext package and make sure  msgfmt is in the path.])
  fi

  FZ_PATH_PROG_VER(msgmerge, msgmerge, 0.11.0)
  if test -z "$msgmerge"; then
    AC_MSG_ERROR([msgmerge not found or too old, please install a current version of the GNU gettext package and make sure msgmerge is in the path.])
  fi

  AC_MSG_CHECKING([available FZ3 message catalogs])

  FILEZILLA_LINGUAS_PO=
  FILEZILLA_LINGUAS=

  if /bin/ls > /dev/null; then
    LSPROG='/bin/ls'
  else
    LSPROG='ls'
  fi

  if test x$srcdir = x; then
    path="locales"
  else
    path="$srcdir/locales"
  fi
  for i in `$LSPROG $path/*.po`; do
    i=${i%.*}
    i=${i##*/}
    FILEZILLA_LINGUAS_PO=$FILEZILLA_LINGUAS_PO${FILEZILLA_LINGUAS_PO:+ }\$\(srcdir\)/$i.po
    FILEZILLA_LINGUAS_PO_NEW=$FILEZILLA_LINGUAS_PO_NEW${FILEZILLA_LINGUAS_PO_NEW:+ }$i.po.new
    FILEZILLA_LINGUAS=$FILEZILLA_LINGUAS${FILEZILLA_LINGUAS:+ }$i
    FILEZILLA_LINGUAS_MO=$FILEZILLA_LINGUAS_MO${FILEZILLA_LINGUAS_MO:+ }$i.mo
  done

  AC_MSG_RESULT([$FILEZILLA_LINGUAS])
fi

AC_SUBST(FILEZILLA_LINGUAS)
AC_SUBST(FILEZILLA_LINGUAS_PO)
AC_SUBST(FILEZILLA_LINGUAS_PO_NEW)
AC_SUBST(FILEZILLA_LINGUAS_MO)


# Output automake conditionals
# ----------------------------

AM_CONDITIONAL(FZ_WINDOWS, test "$windows" = "1")
AM_CONDITIONAL(FZ_MAC, test "$mac" = "1")
AM_CONDITIONAL(FZ_UNIX, test "$unix" = "1")
AM_CONDITIONAL([LOCALES_ONLY], [test "$localesonly" = "yes"])
AM_CONDITIONAL([LOCALES], [test "$locales" = "yes"])
AM_CONDITIONAL(USE_RESOURCEFILE, test "$use_resourcefile" = "true")
AM_CONDITIONAL(MACAPPBUNDLE, [test "$macappbundle" = "yes"])
AM_CONDITIONAL(USE_MAC_SANDBOX, [test "$use_mac_sandbox" = "yes"])
AM_CONDITIONAL(MAKENSISSCRIPT, [test "$makensisscript" = "yes"])
AM_CONDITIONAL(ENABLE_PRECOMP, test "x$use_precomp" = "xyes")
AM_CONDITIONAL(HAS_CPPUNIT, [test "$has_cppunit" = "yes"])
AM_CONDITIONAL(HAVE_LIBPUGIXML, [test "x$with_pugixml" = "xsystem"])
AM_CONDITIONAL(HAVE_DBUS, [test "x$with_dbus" = "xyes"])
AM_CONDITIONAL(ENABLE_STORJ, [test "x$enable_storj" = "xyes"])
AM_CONDITIONAL(ENABLE_FTP, [test "x$enable_ftp" = "xyes"])
AM_CONDITIONAL(ENABLE_SFTP, [test "x$enable_sftp" = "xyes"])
AM_CONDITIONAL(ENABLE_GUI, [test "x$enable_gui" = "xyes"])

AC_CONFIG_FILES(Makefile src/Makefile src/engine/Makefile src/pugixml/Makefile
src/dbus/Makefile
src/commonui/Makefile
src/interface/Makefile src/interface/resources/Makefile src/include/Makefile
locales/Makefile
data/Makefile
src/interface/resources/classic/Makefile
src/interface/resources/cyril/Makefile
src/interface/resources/cyril/16x16/Makefile
src/interface/resources/blukis/Makefile
src/interface/resources/default/Makefile
src/interface/resources/flatzilla/Makefile
src/interface/resources/lone/Makefile
src/interface/resources/minimal/Makefile
src/interface/resources/opencrystal/Makefile
src/interface/resources/sun/Makefile
src/interface/resources/tango/Makefile
src/storj/Makefile
tests/Makefile
src/interface/resources/version.rc
src/interface/resources/MacInfo.plist
src/fzshellext/version.rc)
AC_OUTPUT
