#ifndef RENDER_PS2_H
#define RENDER_PS2_H

#include <stdint.h>

// PS2-only additions to render.h.

// One texel of a texture, as the GS's ABGR1555 (the game's colours with red
// and blue swapped). memcard_ps2.c builds the save icon from the ship model's
// textures with it. Returns 0 outside the texture.
uint16_t render_ps2_texel(uint16_t texture_index, int x, int y);

#endif
