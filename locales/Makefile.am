CLEANFILES = *.po.new *.mo filezilla.pot filezilla.pot.new filezilla.pot~

dist_noinst_DATA = filezilla.pot @FILEZILLA_LINGUAS_PO@

install-data-local: install-filezilla-catalogs

install-filezilla-catalogs: allmo
	for i in @FILEZILLA_LINGUAS@ ; do \
	  $(mkinstalldirs) $(DESTDIR)$(localedir)/$$i/LC_MESSAGES ; \
	  $(INSTALL_DATA) $$i.mo $(DESTDIR)$(localedir)/$$i/LC_MESSAGES/filezilla.mo ; \
	done

uninstall-local:
	for i in @FILEZILLA_LINGUAS@ ; do \
	  rm $(DESTDIR)$(localedir)/$$i/LC_MESSAGES/filezilla.mo ; \
	done


# Unused code
#WXWIN_LINGUAS  =    de

#install-wxstd-catalogs:
#	for i in $(WXWIN_LINGUAS) ; do \
#	  $(mkinstalldirs) $(DESTDIR)$(localedir)/$$i ; \
#	  $(INSTALL_DATA) wxwin/$$i.mo $(DESTDIR)$(localedir)/$$i/filezilla-wxstd.mo ; \
#	done



# ----------------------------------------------------------------------------
# Logic for catalogs updating follows
# Try to not modify catalog files if nothing has changed
# But touch them to make sure the timestamps are correct
# so that the uptodate checks are only made if neccessary.
# ----------------------------------------------------------------------------

# the programs we use (TODO: use configure to detect them)
MSGFMT=@msgfmt@ -c --verbose --no-hash
MSGMERGE=@msgmerge@ --no-fuzzy-matching
XGETTEXT=@xgettext@

# common xgettext args: C++ syntax, use the specified macro names as markers
XGETTEXT_ARGS=-C -k_ -kfztranslate:1,1t -kfztranslate:1,2,3t -kfztranslate_mark:1,1t -kfztranslate_mark:1,2,3t -kwxPLURAL:1,2 -s -j -c@translator --msgid-bugs-address='https://trac.filezilla-project.org/'

# implicit rules
%.mo: %.po.new
	$(MSGFMT) -o $@ $< || exit 1

# a PO file must be updated from wxstd.po to include new translations
%.po.new: $(srcdir)/%.po filezilla.pot
	file=$@; \
	cat $(srcdir)/$${file%.po.new}.po > $@; \
	if [ ! -f $@ ]; then \
	  exit 1; \
	fi; \
	if [ "$${file%.po.new}" != "en" ]; then \
	  $(MSGMERGE) $@ filezilla.pot -o $@~ || exit 1; \
	  mv $@~ $@; \
	fi

# touch file so that timestamp is newer than that of filezilla.pot
	@touch $@;

filezilla.pot: $(srcdir)/../src/include/*.h $(srcdir)/../src/engine/*.h $(srcdir)/../src/interface/*.h  $(srcdir)/../src/engine/*.cpp $(srcdir)/../src/interface/*.cpp
	@rm -f $@.new
	@touch $@.new

	FILES="`find $(srcdir)/../src -name '*.h' -o -name '*.cpp'`"; \
	$(XGETTEXT) $(XGETTEXT_ARGS) -o $@.new $$FILES || exit 1

# change charset declaration in template file to UTF-8
	cat $@.new | sed -e "s/^\"Content-Type: .*$$/\"Content-Type: text\\/plain; charset=UTF-8\\\\n\"/" > $@.new~
	@mv $@.new~ $@.new

	if [ -f $@ ]; then \
	  touch $@; \
	  $(MSGMERGE) -U $@ $@.new || exit 1; \
	else \
	  mv $@.new $@; \
	fi;

	@if [ -f $@.pot~ ]; then rm $@~; fi
	@if [ -f $@.new ]; then rm $@.new; fi

allpo: @FILEZILLA_LINGUAS_PO_NEW@

# this target which will not be called automatically will update the .po files
updatepo: @FILEZILLA_LINGUAS_PO_NEW@
	for t in @FILEZILLA_LINGUAS@; do \
          cp $$t.po.new $(srcdir)/$$t.po || exit 1; \
        done

allmo: @FILEZILLA_LINGUAS_MO@

# on the Mac copy the .mo files into the application bundle
if MACAPPBUNDLE

noinst_DATA = $(top_builddir)/FileZilla.app/Contents/SharedSupport/locales

$(top_builddir)/FileZilla.app/Contents/SharedSupport/locales: allmo
	mkdir -p "$(top_builddir)/FileZilla.app/Contents/SharedSupport/locales"
	for i in @FILEZILLA_LINGUAS@ ; do \
	  mkdir -p "$(top_builddir)/FileZilla.app/Contents/Resources/$$i.lproj"; \
	  mkdir -p "$(top_builddir)/FileZilla.app/Contents/SharedSupport/locales/$$i"; \
	  cp -f "$$i.mo" "$(top_builddir)/FileZilla.app/Contents/SharedSupport/locales/$$i/filezilla.mo"; \
	done
	mkdir -p $(top_builddir)/FileZilla.app/Contents/Resources/en.lproj
	lfz_prefix=`$(PKG_CONFIG) --variable=prefix libfilezilla`; \
	for i in "$${lfz_prefix}"/share/locale/*/LC_MESSAGES/libfilezilla.mo; do \
	  locale=`echo $$i | sed 's/.*\/\([^\/\]*\)\/LC_MESSAGES\/libfilezilla.mo/\1/'`; \
	  mkdir -p "$(top_builddir)/FileZilla.app/Contents/SharedSupport/locales/$$locale"; \
	  cp -f "$$i" "$(top_builddir)/FileZilla.app/Contents/SharedSupport/locales/$$locale/libfilezilla.mo"; \
	done

endif

all: allmo

.PHONY: allpo allmo force-update FORCE
