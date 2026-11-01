#include <stdio.h>

#include "prof_ps2.h"
#include "render.h"
#include "wipeout/game.h"
#include "bench_ps2.h"

prof_counters_t prof;

const float prof_depth_band[PROF_DEPTH_BANDS - 1] = {16000, 32000, 48000, 64000, 76000, 96000};

static uint32_t t_start, t_work, t_flushed;

// Sums over the frames since the last report.
static struct {
	uint32_t frames;
	uint64_t work, flush, vsync, dma_wait;
	uint64_t qwords, triangles, render, polys, xform, verts, upload, uploads;
	uint32_t worst; // the slowest frame's work
	uint32_t over;  // frames whose work and final flush missed a 60Hz frame
	uint64_t total;
	uint64_t depth[PROF_DEPTH_BANDS];
	int section_min, section_max, section_worst; // the camera's track section
} sum = {.section_min = -1};

// One frame at 59.94Hz.
#define FRAME_CYCLES (uint32_t)(PROF_HZ / 59.94f)

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
	sum.upload += prof.upload;
	sum.uploads += prof.uploads;
#ifdef BENCH
	bench_frame(t_flushed - t_start, &prof);
#endif
	for (int i = 0; i < PROF_DEPTH_BANDS; i++) {
		sum.depth[i] += prof.depth[i];
	}
	int section = -1;
	if (render_state.in_race && g.camera.section) {
		section = g.camera.section - g.track.sections;
		if (sum.section_min < 0 || section < sum.section_min) {
			sum.section_min = section;
		}
		if (section > sum.section_max) {
			sum.section_max = section;
		}
	}
	if (t_work - t_start > sum.worst) {
		sum.worst = t_work - t_start;
		sum.section_worst = section;
	}
	if (t_flushed - t_start > FRAME_CYCLES) {
		sum.over++;
	}
	sum.total += t_end - t_start;

	if (sum.total >= PROF_HZ) {
		uint32_t n = sum.frames;
		// work includes the DMA waits of flushes made mid-frame.
		printf("prof: %u frames, ms/frame: work %.2f, worst %.2f (upload %.2f for %u textures, render %.2f of which xform %.2f, dma wait %.2f), final flush %.2f, vsync %.2f; %u verts, %u polys, %u tris, %u KB to GS; uncapped %.0f fps, %u frames over budget\n",
			(unsigned)n, (double)ms(sum.work, n), (double)ms(sum.worst, 1), (double)ms(sum.upload, n), (unsigned)sum.uploads, (double)ms(sum.render, n), (double)ms(sum.xform, n), (double)ms(sum.dma_wait, n),
			(double)ms(sum.flush, n), (double)ms(sum.vsync, n),
			(unsigned)(sum.verts / n), (unsigned)(sum.polys / n), (unsigned)(sum.triangles / n), (unsigned)(sum.qwords * 16 / n / 1024),
			// What the frame rate would be without waiting for vsync: the
			// EE's work plus the wait for the GS to take the last packet.
			(double)(PROF_HZ * n / (sum.work + sum.flush)), (unsigned)sum.over);
		// Where the 3D polygons are, to judge draw distances: per frame, by
		// the depth of each one's nearest vertex. Sections are the camera's.
		if (sum.section_min >= 0) {
			uint64_t *d = sum.depth;
			printf("depth: polys/frame <16k %u, <32k %u, <48k %u, <64k %u, <76k %u, <96k %u, beyond %u; sections %d-%d, worst frame at %d\n",
				(unsigned)(d[0] / n), (unsigned)(d[1] / n), (unsigned)(d[2] / n), (unsigned)(d[3] / n),
				(unsigned)(d[4] / n), (unsigned)(d[5] / n), (unsigned)(d[6] / n),
				sum.section_min, sum.section_max, sum.section_worst);
		}
		sum = (typeof(sum)){.section_min = -1};
	}
}
