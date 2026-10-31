#include <stdio.h>

#include "prof_ps2.h"

prof_counters_t prof;

static uint32_t t_start, t_work, t_flushed;

// Sums over the frames since the last report.
static struct {
	uint32_t frames;
	uint64_t work, flush, vsync, dma_wait;
	uint64_t qwords, triangles, render, polys, xform, verts;
	uint64_t total;
} sum;

static float ms(uint64_t cycles, uint32_t frames) {
	return cycles * 1000.0f / PROF_HZ / frames;
}

void prof_frame_start(void) {
	t_start = prof_cycles();
	prof = (prof_counters_t){0};
}

void prof_frame_work_done(void) {
	t_work = prof_cycles();
}

void prof_frame_flushed(void) {
	t_flushed = prof_cycles();
}

void prof_frame_end(void) {
	uint32_t t_end = prof_cycles();
	sum.frames++;
	sum.work += t_work - t_start;
	sum.flush += t_flushed - t_work;
	sum.vsync += t_end - t_flushed;
	sum.dma_wait += prof.dma_wait;
	sum.qwords += prof.qwords;
	sum.triangles += prof.triangles;
	sum.render += prof.render;
	sum.polys += prof.polys;
	sum.xform += prof.xform;
	sum.verts += prof.verts;
	sum.total += t_end - t_start;

	if (sum.total >= PROF_HZ) {
		uint32_t n = sum.frames;
		// work includes the DMA waits of flushes made mid-frame.
		printf("prof: %u frames, ms/frame: work %.2f (render %.2f of which xform %.2f, dma wait %.2f), final flush %.2f, vsync %.2f; %u verts, %u polys, %u tris, %u KB to GS\n",
			(unsigned)n, (double)ms(sum.work, n), (double)ms(sum.render, n), (double)ms(sum.xform, n), (double)ms(sum.dma_wait, n),
			(double)ms(sum.flush, n), (double)ms(sum.vsync, n),
			(unsigned)(sum.verts / n), (unsigned)(sum.polys / n), (unsigned)(sum.triangles / n), (unsigned)(sum.qwords * 16 / n / 1024));
		sum = (typeof(sum)){0};
	}
}
