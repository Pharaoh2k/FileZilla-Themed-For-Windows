if FZ_WINDOWS
if ENABLE_GUI
  MAYBE_FZSHELLEXT = fzshellext/32 fzshellext/64
endif
endif

if HAVE_LIBPUGIXML
else
  MAYBE_PUGIXML = pugixml
endif

if HAVE_DBUS
  MAYBE_DBUS = dbus
endif

if ENABLE_STORJ
  MAYBE_STORJ = storj
endif

if ENABLE_GUI
  MAYBE_GUI = interface
endif

SUBDIRS = include engine $(MAYBE_PUGIXML) $(MAYBE_DBUS) commonui $(MAYBE_GUI) $(MAYBE_STORJ) $(MAYBE_FZSHELLEXT) .
DIST_SUBDIRS = include engine pugixml dbus commonui interface storj fzshellext/64 .

dist-hook:
	mkdir -p $(distdir)/fzshellext/32
	mv -f $(distdir)/fzshellext/64/* $(distdir)/fzshellext
	rm -rf $(distdir)/config
	rm -rf $(distdir)/m4

if MACAPPBUNDLE
if ENABLE_GUI

noinst_DATA = dylibs.copied

dylibs.copied: $(top_builddir)/FileZilla.app/Contents/MacOS/*
	"$(top_srcdir)/data/dylibcopy.sh" "$(top_builddir)/FileZilla.app" "$(top_builddir)/src/engine:$(top_builddir)/src/commonui"
	@touch dylibs.copied

CLEANFILES = dylibs.copied

endif
endif
