# The music streamer, an IOP module (see music.c). Built by the main Makefile
# into src/irx/music_irx.c.

IOP_BIN = music.irx
IOP_OBJS = music.o imports.o
IOP_OBJS_DIR = obj/

all: $(IOP_BIN)

clean:
	rm -rf $(IOP_BIN) obj

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal
