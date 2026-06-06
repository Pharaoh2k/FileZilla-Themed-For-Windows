# Rules for the test code (use `make check` to execute)

if ENABLE_GUI
  MAYBE_GUI_TEST = gui_test
endif

TESTS = test $(MAYBE_GUI_TEST)
check_PROGRAMS = $(TESTS)

test_SOURCES = \
	test.cpp \
	dirparsertest.cpp \
	localpathtest.cpp \
	serverpathtest.cpp

test_CPPFLAGS = -I$(top_builddir)/config
test_CPPFLAGS += $(LIBFILEZILLA_CFLAGS)
test_CPPFLAGS += $(LIBFZSSH_CFLAGS)
test_CXXFLAGS = $(CPPUNIT_CFLAGS)

test_LDFLAGS = ../src/engine/libfzclient-private.la
test_LDFLAGS += $(LIBFILEZILLA_LIBS)
test_LDFLAGS += $(LIBFZSSH_LIBS)
test_LDFLAGS += $(PUGIXML_LIBS)
test_LDFLAGS += $(LIBGNUTLS_LIBS)
test_LDFLAGS += $(IDN_LIB)
test_LDFLAGS += $(LIBSQLITE3_LIBS)
test_LDFLAGS += $(CPPUNIT_LIBS)

test_DEPENDENCIES = ../src/engine/libfzclient-private.la

if ENABLE_GUI

gui_test_SOURCES = \
	cmpnatural.cpp \
	gui_test.cpp

gui_test_CPPFLAGS = -I$(top_builddir)/config
gui_test_CPPFLAGS += $(LIBFILEZILLA_CFLAGS)
gui_test_CPPFLAGS += $(LIBFZSSH_CFLAGS)
gui_test_CPPFLAGS += $(WX_CPPFLAGS)
gui_test_CXXFLAGS = $(WX_CXXFLAGS_ONLY) $(CPPUNIT_CFLAGS)

gui_test_LDFLAGS = ../src/engine/libfzclient-private.la
gui_test_LDFLAGS += $(LIBFILEZILLA_LIBS)
gui_test_LDFLAGS += $(LIBFZSSH_LIBS)
gui_test_LDFLAGS += $(LIBGNUTLS_LIBS)
gui_test_LDFLAGS += $(WX_LIBS)
gui_test_LDFLAGS += $(IDN_LIB)
gui_test_LDFLAGS += $(LIBSQLITE3_LIBS)
gui_test_LDFLAGS += $(CPPUNIT_LIBS)
gui_test_LDFLAGS += $(PUGIXML_LIBS)
gui_test_DEPENDENCIES = ../src/engine/libfzclient-private.la

endif

