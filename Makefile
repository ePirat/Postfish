# Fuck Automake
# Fuck the horse it rode in on
# and Fuck its little dog Libtool too

CC=gcc
LD=gcc
INSTALL=install
PREFIX=/usr/local
BINDIR=$PREFIX/bin
ETCDIR=/etc
MANDIR=$PREFIX/man

# is this a platform that uses IEEE 754/854 32 bit floats?  The
# following is good for a speedup on most of these systems, otherwise
# comment it out.  Using this define on a system where a 'float' is
# *not* an IEEE 32 bit float will destroy, destroy, destroy the audio.

IEEE=-DNASTY_IEEE_FLOAT32_HACK_IS_FASTER_THAN_LOG=1




SRC = main.c mainpanel.c multibar.c readout.c input.c output.c clippanel.c \
	declip.c reconstruct.c multicompand.c windowbutton.c subpanel.c \
	feedback.c freq.c eq.c eqpanel.c compandpanel.c subband.c lpc.c
OBJ = main.o mainpanel.o multibar.o readout.o input.o output.o clippanel.o \
	declip.o reconstruct.o multicompand.o windowbutton.o subpanel.o \
	feedback.o freq.o eq.o eqpanel.o compandpanel.o subband.o lpc.o
GCF = `pkg-config --cflags gtk+-2.0` -DG_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED

all:	
	$(MAKE) target CFLAGS="-W -O3 -ffast-math $(GCF) $(IEEE)"

debug:
	$(MAKE) target CFLAGS="-g -W -D__NO_MATH_INLINES $(GCF) $(IEEE)"

profile:
	$(MAKE) target CFLAGS="-W -pg -g -O3 -ffast-math $(GCF) $(IEEE)" LIBS="-lgprof-helper"

clean:
	rm -f $(OBJ) *.d gmon.out

%.d: %.c
	$(CC) -M $(GCF) $< > $@.$$$$; sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; rm -f $@.$$$$

ifneq ($(MAKECMDGOALS),clean)
include $(SRC:.c=.d)
endif

target: $(OBJ) 
	./touch-version
	$(LD) $(OBJ) $(CFLAGS) -o postfish $(LIBS) `pkg-config --libs gtk+-2.0` -lpthread -lfftw3f -lm

install:
	$(INSTALL) -d -m 0755 $(BINDIR)
	$(INSTALL) -m 0755 postfish $(BINDIR)
	$(INSTALL) -d -m 0755 $(ETCDIR)
	$(INSTALL) -m 0644 postfish-gtkrc $(ETCDIR)
#	$(INSTALL) -d -m 0755 $(MANDIR)
#	$(INSTALL) -d -m 0755 $(MANDIR)/man1
#	$(INSTALL) -m 0644 postfish.1 $(MANDIR)/man1
