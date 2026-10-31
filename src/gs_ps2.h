#ifndef GS_PS2_H
#define GS_PS2_H

// A small GIF packet builder for the GS. Register writes and primitives are
// packed into one buffer (GIF A+D format) and sent to the GS by DMA when it
// fills up, or on gs_flush(). gsKit still owns display setup, clear and flip.

#include <tamtypes.h>

// A vertex ready for the GS: colour (RGBAQ), texture coordinates and screen
// position (XYZ2, 12.4 fixed point with the GS's drawing offset applied).
// uv is sent only for textured primitives: as ST (floats, divided by the Q in
// rgbaq) or, if the primitive has FST set, as UV (texels, 10.4 fixed point).
typedef struct {
	u64 rgbaq;
	u64 uv;
	u64 xyz;
} gs_vertex_t;

void gs_packet_init(void);

// Queue a write of a GS register (GS_REG_* from gs_gp.h).
void gs_set_reg(u64 reg, u64 value);

// Queue a primitive: prim is a GS_SET_PRIM value, followed by n vertices.
void gs_draw(u64 prim, const gs_vertex_t *v, int n);

// Copy a w x h image from RAM to VRAM at block address dbp (256-byte units),
// with a buffer width of dbw (64-pixel units). Everything queued is sent
// first, so the GS draws it before the image lands. pixels must be 16-byte
// aligned and a whole number of qwords long. Queue a TEXFLUSH before drawing
// with the new image.
void gs_upload(u32 dbp, u32 dbw, u32 psm, int w, int h, const void *pixels, u32 bytes);

// Send everything queued and wait until the GS has received it.
void gs_flush(void);

#endif
