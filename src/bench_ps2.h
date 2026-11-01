#ifndef BENCH_PS2_H
#define BENCH_PS2_H

#include <stdint.h>

#include "prof_ps2.h"

// Benchmark builds (make BENCH=1 for track02, BENCH=all for every circuit).
// The game boots straight into a race with the AI driving the player's ship,
// the camera behind it, weapons off, a fixed random seed and a fixed 1/60s
// game tick, so every run plays out the same however long frames take. Each
// track logs a "bench:" line for its intro and one for its first lap, then
// "bench: done" after the last.

#ifdef BENCH
void bench_start(void);
// Once a frame, after system_update().
void bench_update(void);
// From prof_frame_end(): this frame's work (to the final flush) in cycles.
void bench_frame(uint32_t work, const prof_counters_t *counters);
#endif

#endif
