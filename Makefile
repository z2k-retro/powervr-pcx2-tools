# Builds the tools into bin/. Linux only: the card tools use sysfs and VFIO;
# pcxsim builds anywhere with a C99 compiler and pthreads.

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
CPPFLAGS += -Irenderer -Itools
RENDER  = renderer/vid_powervr_pcx_render.c

all: bin/pcx2dump bin/pcx2replay bin/pcx2play bin/pcxsim

bin:
	mkdir -p bin

bin/pcx2dump: tools/pcx2dump.c tools/pcx2pci.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tools/pcx2dump.c

bin/pcx2replay: tools/pcx2replay.c tools/pcx2pci.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tools/pcx2replay.c

bin/pcx2play: tools/pcx2play.c tools/pcx2pci.h $(RENDER) renderer/*.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tools/pcx2play.c $(RENDER) -lm

bin/pcxsim: tools/pcxsim.c $(RENDER) renderer/*.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tools/pcxsim.c $(RENDER) -lm -lpthread

clean:
	rm -rf bin

.PHONY: all clean
