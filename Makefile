# PS2 build (ps2sdk). Run through tools/build_and_run.sh, or in the container:
#   make
# The Dreamcast build is Makefile.dc.

EE_BIN = wipeout.elf

EE_OBJS = \
	src/alloc.o src/input.o src/mem.o src/system.o src/types.o src/utils.o \
	src/platform_ps2.o src/render_ps2.o src/sfx_ps2.o src/mtx_ps2.o \
	src/wipeout/camera.o src/wipeout/droid.o src/wipeout/game.o src/wipeout/hud.o \
	src/wipeout/image.o src/wipeout/ingame_menus.o src/wipeout/intro.o \
	src/wipeout/main_menu.o src/wipeout/menu.o src/wipeout/object.o \
	src/wipeout/particle.o src/wipeout/race.o src/wipeout/scene.o src/wipeout/ship.o \
	src/wipeout/ship_ai.o src/wipeout/ship_player.o src/wipeout/title.o \
	src/wipeout/track.o src/wipeout/ui.o src/wipeout/weapon.o

# Display output: 480i (any TV), 480p (component cables),
# vga (VGA monitor through the PS2 Linux kit adapter). e.g. make VIDEO=vga
VIDEO ?= 480i
EE_CFLAGS += -DVIDEO_$(VIDEO)

EE_INCS += -Isrc -I$(GSKIT)/include
EE_LIBS += -L$(GSKIT)/lib -lgskit -ldmakit -ldebug -lm

all: $(EE_BIN)

# Rebuild the renderer when VIDEO changes.
VIDEO_STAMP = src/.video-$(VIDEO)
$(VIDEO_STAMP):
	rm -f src/.video-*
	touch $@
src/render_ps2.o: $(VIDEO_STAMP)

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
