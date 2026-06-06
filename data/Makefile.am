dist_noinst_DATA = install.nsi.in \
		dllcopy.sh \
		dylibcopy.sh \
		makezip.sh.in \
		nsis_appid.dll \
		nsis_appid.txt \
		process_running.nsh \
		UAC.nsh \
		UAC.dll \
		UAC_unicode.dll.txt \
		uninstall.ico

if MAKENSISSCRIPT

noinst_DATA = $(MAYBE_LANGFILES) libtoolexecutablesubdir.nsh

if LOCALES
  MAYBE_LANGFILES = installlangfiles.nsh uninstalllangfiles.nsh
endif

noinst_DATA += dll_gui_install.nsh

dll_gui_install.nsh: Makefile
	@rm -f dll_gui_install.nsh
	@rm -f dll_gui_uninstall.nsh
	@rm -rf dlls_gui
	@mkdir -p dlls_gui
	"$(srcdir)/dllcopy.sh" gui "$(top_builddir)/src/interface" filezilla.exe "$(OBJDUMP)" "$(CXX)" "../src/engine/:../src/commonui/:$$PATH" "$(WX_LIBS)"
	if test -f "${top_builddir}/src/storj/fzstorj.exe"; then \
		"$(srcdir)/dllcopy.sh" gui "$(top_builddir)/src/storj" fzstorj.exe "$(OBJDUMP)" "$(CXX)" $$PATH; \
	fi

installlangfiles.nsh: Makefile
	rm -f $@
	for i in @FILEZILLA_LINGUAS@; do\
	  echo !insertmacro INSTALLLANGFILE ..\\locales\\$$i.mo $$i filezilla.mo >> $@; \
	done
	lfz_prefix=`$(PKG_CONFIG) --variable=prefix libfilezilla`; \
	for i in "$${lfz_prefix}"/share/locale/*/LC_MESSAGES/libfilezilla.mo; do \
	  locale=`echo $$i | sed 's/.*\/\([^\/\]*\)\/LC_MESSAGES\/libfilezilla.mo/\1/'`; \
	  echo !insertmacro INSTALLLANGFILE "`echo $$i | sed 's/\\//\\\\/g'`" "$$locale" libfilezilla.mo >> $@; \
	done

uninstalllangfiles.nsh: Makefile
	rm -f $@
	for i in @FILEZILLA_LINGUAS@; do\
	  echo !insertmacro UNINSTALLLANGFILE $$i filezilla.mo >> $@; \
	done
	lfz_prefix=`$(PKG_CONFIG) --variable=prefix libfilezilla`; \
	for i in "$${lfz_prefix}"/share/locale/*/LC_MESSAGES/libfilezilla.mo; do \
	  locale=`echo $$i | sed 's/.*\/\([^\/\]*\)\/LC_MESSAGES\/libfilezilla.mo/\1/'`; \
	  echo !insertmacro UNINSTALLLANGFILE "$$locale" libfilezilla.mo >> $@; \
	done

# Some versions of libtool create executables as foo.exe
# while otheres create executables as .libs/foo.exe
# NSIS needs to know the actual path.

libtoolexecutablesubdir.nsh: Makefile
	rm -f $@
	touch test_libtool_exedir.c
	$(CC) -c -o test_libtool_exedir.o test_libtool_exedir.c
	exedir=`$(LIBTOOL) -n --mode=link $(CC) -o test_libtool_exedir$(EXEEXT) test_libtool_exedir.o`; \
	if echo "$$exedir" | grep -- '-o \.libs' > /dev/null; then \
	  echo "!define LT_EXEDIR \".libs\\\"" > libtoolexecutablesubdir.nsh; \
	else \
	  touch libtoolexecutablesubdir.nsh; \
	fi
	rm -f test_libtool_exedir.o test_libtool_exedir.c

CLEANFILES = installlangfiles.nsh uninstalllangfiles.nsh libtoolexecutablesubdir.nsh dll_gui_install.nsh dll_gui_uninstall.nsh

endif

desktopentrydir = $(datadir)/applications
dist_desktopentry_DATA = filezilla.desktop

appdatadir = $(datadir)/appdata
dist_appdata_DATA = filezilla.appdata.xml

clean-local:
	rm -rf dlls_gui

