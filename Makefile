# Fuck Automake
# Fuck the horse it rode in on
# and Fuck its little dog Libtool too

CC=gcc
LD=gcc

SRC = main.c mainpanel.c multibar.c readout.c input.c output.c clippanel.c declip.c \
	reconstruct.c smallft.c
OBJ = main.o mainpanel.o multibar.o readout.o input.o output.o clippanel.o declip.o \
	reconstruct.o smallft.o
GCF = `pkg-config --cflags gtk+-2.0` -DG_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED

all:	
	$(MAKE) target CFLAGS="-W -O2 $(GCF)"

debug:
	$(MAKE) target CFLAGS="-g -W -D__NO_MATH_INLINES $(GCF)"

profile:
	$(MAKE) target CFLAGS="-W -pg -g -O2 $(GCF)"

clean:
	rm -f $(OBJ) *.d 

%.d: %.c
	$(CC) -M $(GCF) $< > $@.$$$$; sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; rm -f $@.$$$$

include $(SRC:.c=.d)

target: $(OBJ)
	./touch-version
	$(LD) $(OBJ) $(CFLAGS) -o postfish `pkg-config --libs gtk+-2.0` -lpthread -lm

