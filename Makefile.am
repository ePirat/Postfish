## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)/include 

LDADD = -lm -lpthread -lncurses

bin_PROGRAMS = postfish

postfish_SOURCES = postfish.c smallft.c form.c smallft.h form.h
postfish_LDADD = $(LDADD)

debug:
	$(MAKE) all CFLAGS="@DEBUG@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@"

EXTRA_DIST = COPYING autogen.sh configure.in Makefile.am