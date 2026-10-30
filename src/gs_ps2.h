#ifndef GS_PS2_H
#define GS_PS2_H

// A small GIF packet builder for the GS. Register writes and primitives are
// packed into one buffer (GIF A+D format) and sent to the GS by DMA when it
// fills up, or on gs_flush(). gsKit still owns display setup, clear and flip.

#include <tamtypes.h>

// A vertex ready for the GS: colour (RGBAQ) and screen position (XYZ2,
// 12.4 fixed point with the GS's drawing offset applied).
typedef struct {
	u64 rgbaq;
	u64 xyz;
} gs_vertex_t;

void gs_packet_init(void);

// Queue a write of a GS register (GS_REG_* from gs_gp.h).
void gs_set_reg(u64 reg, u64 value);

// Queue a primitive: prim is a GS_SET_PRIM value, followed by n vertices.
void gs_draw(u64 prim, const gs_vertex_t *v, int n);

// Send everything queued and wait until the GS has received it.
void gs_flush(void);

#endif
