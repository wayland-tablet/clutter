include $(top_srcdir)/build/autotools/Makefile.am.silent

source_c = \
	cogl-pango-display-list.c \
	cogl-pango-fontmap.c    \
	cogl-pango-render.c     \
	cogl-pango-glyph-cache.c

source_h = cogl-pango.h

source_h_priv = \
	cogl-pango-display-list.h \
	cogl-pango-private.h    \
	cogl-pango-glyph-cache.h

if COGL_STANDALONE_BUILD
lib_LTLIBRARIES = libcoglpango.la
else
noinst_LTLIBRARIES = libcoglpango.la
endif

libcoglpango_la_SOURCES = $(source_c) $(source_h) $(source_h_priv)
libcoglpango_la_CFLAGS = $(COGL_DEP_CFLAGS) $(COGL_EXTRA_CFLAGS) $(MAINTAINER_CFLAGS)
libcoglpango_la_LIBADD = $(COGL_DEP_LIBS)
if COGL_STANDALONE_BUILD
libcoglpango_la_LIBADD += $(top_builddir)/cogl/libcogl.la
libcoglpango_la_LDFLAGS = \
	-export-dynamic \
	-export-symbols-regex "^cogl.*"
	-no-undefined \
	-version-info @COGL_LT_CURRENT@:@COGL_LT_REVISION@:@COGL_LT_AGE@
endif

INCLUDES = \
	-DG_DISABLE_SINGLE_INCLUDES	\
	-DCLUTTER_COMPILATION		\
	-DG_LOG_DOMAIN=\"CoglPango\"	\
	-I$(top_srcdir)
if !COGL_STANDALONE_BUILD
INCLUDES += \
	-I$(top_srcdir)/clutter		\
	-I$(top_srcdir)/clutter/cogl	\
	-I$(top_builddir)/clutter	\
	-I$(top_builddir)/clutter/cogl
endif

if COGL_STANDALONE_BUILD
coglpangoheadersdir = $(includedir)/cogl/cogl
coglpangoheaders_HEADERS = $(source_h)
else
coglpangoheadersdir = $(includedir)/clutter-@CLUTTER_API_VERSION@/cogl
coglpangoheaders_HEADERS = $(source_h)
endif

dist-hook: ../../../build/win32/vs9/cogl-pango.vcproj ../../../build/win32/vs10/cogl-pango.vcxproj ../../../build/win32/vs10/cogl-pango.vcxproj.filters

../../../build/win32/vs9/cogl-pango.vcproj: $(top_srcdir)/build/win32/vs9/cogl-pango.vcprojin
	for F in $(source_c); do \
		case $$F in \
		*.c)	echo '   <File RelativePath="..\..\..\clutter\cogl\pango\'$$F'" />' \
			;; \
		esac; \
	done >coglpango.sourcefiles
	$(CPP) -P - <$(top_srcdir)/build/win32/vs9/cogl-pango.vcprojin >$@
	rm coglpango.sourcefiles
	
../../../build/win32/vs10/cogl-pango.vcxproj: $(top_srcdir)/build/win32/vs10/cogl-pango.vcxprojin
	for F in $(source_c); do \
		case $$F in \
		*.c)	echo '    <ClCompile Include="..\..\..\clutter\cogl\pango\'$$F'" />' \
			;; \
		esac; \
	done >coglpango.vs10.sourcefiles
	$(CPP) -P - <$(top_srcdir)/build/win32/vs10/cogl-pango.vcxprojin >$@
	rm coglpango.vs10.sourcefiles
	
../../../build/win32/vs10/cogl-pango.vcxproj.filters: $(top_srcdir)/build/win32/vs10/cogl-pango.vcxproj.filtersin
	for F in $(source_c); do \
		case $$F in \
		*.c)	echo '    <ClCompile Include="..\..\..\clutter\cogl\pango\'$$F'"><Filter>Sources</Filter></ClCompile>' \
			;; \
		esac; \
	done >coglpango.vs10.sourcefiles.filters
	$(CPP) -P - <$(top_srcdir)/build/win32/vs10/cogl-pango.vcxproj.filtersin >$@
	rm coglpango.vs10.sourcefiles.filters

EXTRA_DIST = cogl-pango.symbols

# Let the VS9/VS10 Project files be cleared out before they are re-expanded...
DISTCLEANFILES = ../../../build/win32/vs9/cogl-pango.vcproj	\
		  ../../../build/win32/vs10/cogl-pango.vcxproj	\
		  ../../../build/win32/vs10/cogl-pango.vcxproj.filters

