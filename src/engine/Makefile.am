AUTOMAKE_OPTIONS = subdir-objects

lib_LTLIBRARIES = libfzclient-private.la

libfzclient_private_la_CPPFLAGS = -I$(top_builddir)/config
libfzclient_private_la_CPPFLAGS += $(LIBFILEZILLA_CFLAGS)
libfzclient_private_la_CPPFLAGS += $(LIBFZSSH_CFLAGS)
libfzclient_private_la_CPPFLAGS += -DBUILDING_FILEZILLA

libfzclient_private_la_SOURCES = \
		activity_logger.cpp \
		activity_logger_layer.cpp \
		commands.cpp \
		controlsocket.cpp \
		directorycache.cpp \
		directorylisting.cpp \
		directorylistingparser.cpp \
		engine_context.cpp \
		engine_options.cpp \
		engineprivate.cpp \
		externalipresolver.cpp \
		FileZillaEngine.cpp \
		http/filetransfer.cpp \
		http/httpcontrolsocket.cpp \
		http/request.cpp \
		local_path.cpp \
		logfile_writer.cpp \
		logging.cpp \
		lookup.cpp \
		misc.cpp \
		notification.cpp \
		oplock_manager.cpp \
		optionsbase.cpp \
		pathcache.cpp \
		posix_chmod.cpp \
		proxy.cpp \
		rtt.cpp \
		server.cpp \
		servercapabilities.cpp \
		serverpath.cpp\
		sizeformatting.cpp \
		tls.cpp \
		version.cpp \
		xmlutils.cpp

noinst_HEADERS = \
		activity_logger_layer.h \
		controlsocket.h \
		directorycache.h \
		directorylistingparser.h \
		engineprivate.h \
		filezilla.h \
		http/filetransfer.h \
		http/httpcontrolsocket.h \
		http/request.h \
		logging_private.h \
		lookup.h \
		oplock_manager.h \
		pathcache.h \
		proxy.h \
		rtt.h \
		servercapabilities.h \
		tls.h

if ENABLE_FTP
libfzclient_private_la_SOURCES += \
		ftp/chmod.cpp \
		ftp/cwd.cpp \
		ftp/delete.cpp \
		ftp/filetransfer.cpp \
		ftp/ftpcontrolsocket.cpp \
		ftp/list.cpp \
		ftp/logon.cpp \
		ftp/mkd.cpp \
		ftp/rawcommand.cpp \
		ftp/rawtransfer.cpp \
		ftp/rename.cpp \
		ftp/rmd.cpp \
		ftp/transfersocket.cpp

noinst_HEADERS += \
		ftp/chmod.h \
		ftp/cwd.h \
		ftp/delete.h \
		ftp/filetransfer.h \
		ftp/ftpcontrolsocket.h \
		ftp/list.h \
		ftp/logon.h \
		ftp/mkd.h \
		ftp/rename.h \
		ftp/rawcommand.h \
		ftp/rawtransfer.h \
		ftp/rmd.h \
		ftp/transfersocket.h
endif

if ENABLE_SFTP
libfzclient_private_la_SOURCES += \
		sftp/chmod.cpp \
		sftp/connect.cpp \
		sftp/cwd.cpp \
		sftp/delete.cpp \
		sftp/filetransfer.cpp \
		sftp/list.cpp \
		sftp/mkd.cpp \
		sftp/rename.cpp \
		sftp/rmd.cpp \
		sftp/sftpcontrolsocket.cpp

noinst_HEADERS += \
		sftp/chmod.h \
		sftp/connect.h \
		sftp/cwd.h \
		sftp/delete.h \
		sftp/filetransfer.h \
		sftp/list.h \
		sftp/mkd.h \
		sftp/rename.h \
		sftp/rmd.h \
		sftp/sftpcontrolsocket.h
endif

if ENABLE_STORJ
libfzclient_private_la_SOURCES += \
		storj/connect.cpp \
		storj/delete.cpp \
		storj/file_transfer.cpp \
		storj/input_thread.cpp \
		storj/list.cpp \
		storj/mkd.cpp \
		storj/rmd.cpp \
		storj/storjcontrolsocket.cpp

noinst_HEADERS += \
		storj/connect.h \
		storj/delete.h \
		storj/event.h \
		storj/file_transfer.h \
		storj/input_thread.h \
		storj/list.h \
		storj/mkd.h \
		storj/rmd.h \
		storj/storjcontrolsocket.h
endif

if !HAVE_LIBPUGIXML
libfzclient_private_la_SOURCES += ../pugixml/pugixml.cpp
endif

libfzclient_private_la_CXXFLAGS = -fvisibility=hidden
libfzclient_private_la_LDFLAGS = -no-undefined -release $(ENGINE_VERSION_MAJOR).$(ENGINE_VERSION_MINOR).$(ENGINE_VERSION_MICRO)
libfzclient_private_la_LDFLAGS += $(LIBFZSSH_LIBS) $(LIBFILEZILLA_LIBS)
libfzclient_private_la_LDFLAGS += $(IDN_LIB)

if FZ_MAC
libfzclient_private_la_LDFLAGS += -framework CoreServices
endif

CLEANFILES = filezilla.h.gch
DISTCLEANFILES = ./$(DEPDIR)/filezilla.Po

if ENABLE_PRECOMP

BUILT_SOURCES = filezilla.h.gch

-include ./$(DEPDIR)/filezilla.Po

filezilla.h.gch: filezilla.h
	if $(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(libfzclient_private_la_CPPFLAGS) $(CPPFLAGS) $(libfzclient_private_la_CXXFLAGS) $(CXXFLAGS) -MT $@ -MD -MP -MF "$(DEPDIR)/filezilla.Tpo" -c -o $@ $<; \
	then mv -f "$(DEPDIR)/filezilla.Tpo" "$(DEPDIR)/filezilla.Po"; else rm -f "$(DEPDIR)/filezilla.Tpo"; exit 1; fi

endif

