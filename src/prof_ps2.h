#ifndef PROF_PS2_H
#define PROF_PS2_H

#include <stdint.h>

// Frame profiling on the EE's cycle counter (COP0 Count, 294.912 MHz).
// render_ps2.c marks the frame's phases; gs_ps2.c adds its DMA waits.
// prof_frame_end() logs per-frame averages once a second.

#define PROF_HZ 294912000.0f

static inline uint32_t prof_cycles(void) {
	uint32_t c;
	__asm__ volatile("mfc0 %0, $9" : "=r"(c));
	return c;
}

// Depth bands for prof.depth, in the game's units: the Dreamcast port's draw
// distances are 64000, 76000 and 96000, wipeout-rewrite's 64000 (faded out
// from 48000). The last band is everything beyond.
#define PROF_DEPTH_BANDS 7

typedef struct {
	uint32_t dma_wait;  // cycles waiting for GIF DMA inside the frame
	uint32_t qwords;    // sent to the GS
	uint32_t triangles; // drawn (after clipping)
	uint32_t render;    // cycles in the renderer: transform, clip, packets
	uint32_t polys;     // polygons the game submitted
	uint32_t xform;     // cycles of render spent transforming vertices
	uint32_t verts;     // vertices transformed
	uint32_t upload;    // cycles uploading textures to VRAM
	uint32_t uploads;   // textures uploaded
	uint32_t depth[PROF_DEPTH_BANDS]; // 3D polygons drawn, by nearest vertex's depth
} prof_counters_t;

extern const float prof_depth_band[PROF_DEPTH_BANDS - 1];

extern prof_counters_t prof;

void prof_frame_start(void);
// Call just before the end-of-frame flush, after it, and after vsync.
void prof_frame_work_done(void);
void prof_frame_flushed(void);
void prof_frame_end(void);

#endif
