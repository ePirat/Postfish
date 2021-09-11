# Fuck Automake
# Fuck the horse it rode in on
# and Fuck its little dog Libtool too


# Use the below line to build for PowerPC
# The PPC build *must* use -maltivec, even if the target is a non-altivec machine

#ADD_DEF= -DUGLY_IEEE754_FLOAT32_HACK=1 -maltivec -mcpu=7400

# use the below for x86 and most other platforms where 'float' is 32 bit IEEE754

ADD_DEF= -DUGLY_IEEE754_FLOAT32_HACK=1 

# use the below for anything without IEE754 floats (eg, VAX)

# ADD_DEF=

# GTK deprecations
ADD_DEF += -DGTK_DISABLE_SINGLE_INCLUDES # Disable individual header includes
ADD_DEF += -DGTK_DISABLE_DEPRECATED # Remove all deprecated APIs
ADD_DEF += -DGSEAL_ENABLE # Seal struct members to prevent direct access where not allowed
# TODO: Enable -DGDK_DISABLE_DEPRECATED once no more deprecated GDK APIs are used

CC=gcc 
LD=gcc
INSTALL=install
PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
ETCDIR=/etc/postfish
MANDIR=$(PREFIX)/man

SRC = main.c mainpanel.c multibar.c readout.c input.c output.c clippanel.c \
	declip.c reconstruct.c multicompand.c windowbutton.c subpanel.c \
	feedback.c freq.c eq.c eqpanel.c compandpanel.c subband.c lpc.c \
	bessel.c deverbpanel.c deverb.c singlecomp.c singlepanel.c \
	limit.c limitpanel.c mute.c mixpanel.c mix.c freeverb.c reverbpanel.c \
	outpanel.c config.c window.c follower.c linkage.c
OBJ = $(SRC:.c=.o)

GCF = -DETCDIR=\\\"$(ETCDIR)\\\" `pkg-config --cflags gtk+-2.0 ao \> 1.2`

all:	
	$(MAKE) target CFLAGS="-O2 -ffast-math -fomit-frame-pointer $(GCF) $(ADD_DEF)"

debug:
	$(MAKE) target CFLAGS="-g -Wall -W -Wno-unused-parameter -D__NO_MATH_INLINES $(GCF) $(ADD_DEF)"

profile:
	$(MAKE) target CFLAGS="-pg -g -O2 -ffast-math $(GCF) $(ADD_DEF)" 

clean:
	rm -f $(OBJ) *.d *.d.* gmon.out postfish

distclean: clean
	rm -f postfish-wisdomrc

%.d: %.c
	$(CC) -M $(CFLAGS) $< > $@.$$$$; sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; rm -f $@.$$$$

postfish-wisdomrc:
	fftwf-wisdom -v -o postfish-wisdomrc \
	rif32 rof32 rib32 rob32 \
	rif64 rof64 rib64 rob64 \
	rif128 rof128 rib128 rob128 \
	rif256 rof256 rib256 rob256 \
	rif512 rof512 rib512 rob512 \
	rif1024 rof1024 rib1024 rob1024 \
	rif2048 rof2048 rib2048 rob2048 \
	rif4096 rof4096 rib4096 rob4096 \
	rif8192 rof8192 rib8192 rob8192 \
	rif16384 rof16384 rib16384 rob16384

ifeq ($(MAKECMDGOALS),target)
include $(SRC:.c=.d)
endif

target:  $(OBJ) postfish-wisdomrc
	./touch-version
	$(LD) $(OBJ) $(CFLAGS) -o postfish $(LIBS) `pkg-config --libs gtk+-2.0 \>= 2.24 ao \> 1.2` -lpthread -lfftw3f -lm

install: target
	$(INSTALL) -d -m 0755 $(BINDIR)
	$(INSTALL) -m 0755 postfish $(BINDIR)
	$(INSTALL) -d -m 0755 $(ETCDIR)
	$(INSTALL) -m 0644 postfish-gtkrc $(ETCDIR)
	$(INSTALL) -m 0644 postfish-wisdomrc $(ETCDIR)
#	$(INSTALL) -d -m 0755 $(MANDIR)
#	$(INSTALL) -d -m 0755 $(MANDIR)/man1
#	$(INSTALL) -m 0644 postfish.1 $(MANDIR)/man1
