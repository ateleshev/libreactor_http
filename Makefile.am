ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4
AM_CFLAGS = -std=gnu11 -O3 -flto -fuse-linker-plugin -I$(srcdir)/src/picohttpparser
AM_LDFLAGS = -static

SOURCE_FILES = \
src/reactor_http/reactor_http.c \
src/reactor_http/reactor_http_parser.c \
src/reactor_http/reactor_http_client.c \
src/reactor_http/reactor_http_server.c \
src/picohttpparser/picohttpparser.c

HEADER_FILES = \
src/reactor_http/reactor_http.h \
src/reactor_http/reactor_http_parser.h \
src/reactor_http/reactor_http_client.h \
src/reactor_http/reactor_http_server.h

MAIN_HEADER_FILES = \
src/reactor_http.h

AUTOMAKE_OPTIONS = subdir-objects
lib_LTLIBRARIES = libreactor_http.la
libreactor_http_la_SOURCES = $(SOURCE_FILES) $(HEADER_FILES)

headerfilesdir = $(includedir)/reactor_http
headerfiles_HEADERS = $(HEADER_FILES)

mainheaderdir = $(includedir)
mainheader_HEADERS = $(MAIN_HEADER_FILES)

MAINTAINERCLEANFILES = aclocal.m4 config.h.in configure Makefile.in libreactor_http-?.?.?.tar.gz
maintainer-clean-local:; rm -rf autotools m4 libreactor_http-?.?.?

CLEANFILES = {.,src/reactor_http,src/picohttpparser,test}/*.{gcno,gcda,gcov}
