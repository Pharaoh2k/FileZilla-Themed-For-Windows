AUTOMAKE_OPTIONS = subdir-objects

lib_LTLIBRARIES = libfzclient-commonui-private.la

libfzclient_commonui_private_la_CPPFLAGS = -I$(top_builddir)/config
libfzclient_commonui_private_la_CPPFLAGS += $(LIBFILEZILLA_CFLAGS)
libfzclient_commonui_private_la_CPPFLAGS += $(LIBFZSSH_CFLAGS)
libfzclient_commonui_private_la_CPPFLAGS += -DBUILDING_FZ_COMMONUI

libfzclient_commonui_private_la_CXXFLAGS = -fvisibility=hidden
libfzclient_commonui_private_la_LDFLAGS = -no-undefined -release $(PACKAGE_VERSION_MAJOR).$(PACKAGE_VERSION_MINOR).$(PACKAGE_VERSION_MICRO)
libfzclient_commonui_private_la_LDFLAGS += ../engine/libfzclient-private.la $(LIBFILEZILLA_LIBS) $(PUGIXML_LIBS) $(LIBFZSSH_LIBS)

libfzclient_commonui_private_la_DEPENDENCIES = ../engine/libfzclient-private.la

libfzclient_commonui_private_la_SOURCES = \
	auto_ascii_files.cpp \
	buildinfo.cpp \
	cert_store.cpp \
	chmod_data.cpp \
	file_utils.cpp \
	filter.cpp \
	fz_paths.cpp \
	hostkey_store.cpp \
	ipcmutex.cpp \
	local_recursive_operation.cpp \
	login_manager.cpp \
	misc.cpp \
	remote_recursive_operation.cpp \
	options.cpp \
	protect.cpp \
	site.cpp \
	site_manager.cpp \
	updater.cpp \
	updater_cert.cpp \
	xml_cert_store.cpp \
	xml_file.cpp \
	xmlfunctions.cpp

noinst_HEADERS = \
	auto_ascii_files.h \
	buildinfo.h \
	cert_store.h \
	chmod_data.h \
	file_utils.h \
	filter.h \
	fz_paths.h \
	hostkey_store.h \
	ipcmutex.h \
	local_recursive_operation.h \
	login_manager.h \
	misc.h \
	options.h \
	protect.h \
	recursive_operation.h \
	remote_recursive_operation.h \
	site.h \
	site_color.h \
	site_manager.h \
	updater.h \
	updater_cert.h \
	visibility.h \
	xml_cert_store.h \
	xml_file.h \
	xmlfunctions.h

if FZ_WINDOWS
libfzclient_commonui_private_la_LDFLAGS += -lole32 -Wl,-luuid
endif

if MACAPPBUNDLE

noinst_LTLIBRARIES = libfzclient-commonui-private-mac.la

libfzclient_commonui_private_mac_la_CPPFLAGS = -I$(top_builddir)/config
libfzclient_commonui_private_mac_la_CPPFLAGS += $(LIBFILEZILLA_CFLAGS)
libfzclient_commonui_private_mac_la_CPPFLAGS += -DBUILDING_FZ_COMMONUI

libfzclient_commonui_private_mac_la_LIBTOOLFLAGS = --tag=CXX

libfzclient_commonui_private_mac_la_OBJCXXFLAGS = -fvisibility=hidden

libfzclient_commonui_private_mac_la_SOURCES = \
	fz_paths_osx.mm

libfzclient_commonui_private_mac_la_LDFLAGS = -framework Cocoa

libfzclient_commonui_private_la_LIBADD = libfzclient-commonui-private-mac.la
libfzclient_commonui_private_la_DEPENDENCIES += libfzclient-commonui-private-mac.la

endif
