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

SRC = main.c mainpanel.c multibar.c readout.c input.c output.c clippanel.c \
	declip.c reconstruct.c multicompand.c windowbutton.c subpanel.c \
	feedback.c freq.c eq.c eqpanel.c compandpanel.c subband.c lpc.c
OBJ = main.o mainpanel.o multibar.o readout.o input.o output.o clippanel.o \
	declip.o reconstruct.o multicompand.o windowbutton.o subpanel.o \
	feedback.o freq.o eq.o eqpanel.o compandpanel.o subband.o lpc.o
GCF = `pkg-config --cflags gtk+-2.0` -DG_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED

all:	
	$(MAKE) target CFLAGS="-W -O3 -ffast-math $(GCF)"

debug:
	$(MAKE) target CFLAGS="-g -W -D__NO_MATH_INLINES $(GCF)"

profile:
	$(MAKE) target CFLAGS="-W -pg -g -O3 $(GCF)"

clean:
	rm -f $(OBJ) *.d 

%.d: %.c
	$(CC) -M $(GCF) $< > $@.$$$$; sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; rm -f $@.$$$$

include $(SRC:.c=.d)

target: $(OBJ)
	./touch-version
	$(LD) $(OBJ) $(CFLAGS) -o postfish `pkg-config --libs gtk+-2.0` -lpthread -lfftw3f -lm

install:
	$(INSTALL) -d -m 0755 $(BINDIR)
	$(INSTALL) -m 0755 postfish $(BINDIR)
	$(INSTALL) -d -m 0755 $(ETCDIR)
	$(INSTALL) -m 0644 postfish-gtkrc $(ETCDIR)
#	$(INSTALL) -d -m 0755 $(MANDIR)
#	$(INSTALL) -d -m 0755 $(MANDIR)/man1
#	$(INSTALL) -m 0644 postfish.1 $(MANDIR)/man1
