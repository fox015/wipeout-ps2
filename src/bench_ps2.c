// Benchmark mode: see bench_ps2.h.

#ifdef BENCH

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_ps2.h"
#include "platform.h"
#include "system.h"
#include "wipeout/game.h"
#include "wipeout/camera.h"
#include "wipeout/ship_ai.h"
#include "wipeout/track.h"

#ifndef BENCH_ALL
#define FIRST_CIRCUIT CIRCUT_ALTIMA_VII // track02
#define LAST_CIRCUIT  CIRCUT_ALTIMA_VII
#else
#define FIRST_CIRCUIT 0
#define LAST_CIRCUIT  (NUM_CIRCUTS - 1)
#endif

#define SEED 1995
#define MAX_SECTIONS 1024
#define TIMEOUT_FRAMES (60 * 60 * 5) // a lap that takes 5 minutes is stuck
#define FRAME_CYCLES (uint32_t)(PROF_HZ / 59.94f)

typedef enum {
	PHASE_LOADING, // the race scene hasn't started yet
	PHASE_INTRO,   // the scripted camera fly-by
	PHASE_GRID,    // countdown, not measured
	PHASE_LAP,     // from GO until the first lap is complete
	PHASE_DONE,
} phase_t;

typedef struct {
	uint32_t frames, over;
	uint64_t work, render, polys, qwords;
	uint32_t worst;
} stats_t;

// Every bench: line is also kept here and written to RESULTS_FILE once the
// run is over. On a console the log goes out as UDP, which drops lines over
// Wi-Fi; the file goes through host:, over ps2link's TCP link to ps2client
// (or the emulator's HostFs). Each host: write is a blocking round trip, so
// it's done only after the last measured frame.
#define RESULTS_FILE "bench.txt"
static char results[8192];
static int results_len;

static void bench_log(const char *fmt, ...) {
	char line[256];
	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	if (len < 0) {
		return;
	}
	if (len >= (int)sizeof(line)) {
		len = sizeof(line) - 1;
	}
	printf("%s\n", line);
	if (results_len + len + 1 < (int)sizeof(results)) {
		memcpy(results + results_len, line, len);
		results_len += len;
		results[results_len++] = '\n';
	}
}

// Called after the last measured frame: bench_frame() ignores this one.
static void write_results(void) {
	FILE *f = platform_open_asset(RESULTS_FILE, "w");
	if (!f || fwrite(results, 1, results_len, f) != (size_t)results_len) {
		printf("bench: couldn't write host:%s\n", RESULTS_FILE);
	}
	else {
		printf("bench: results written to host:%s\n", RESULTS_FILE);
	}
	if (f) {
		fclose(f);
	}
}

static phase_t phase;
static int circuit;
static stats_t stats;
static struct {
	uint64_t work;
	uint32_t frames;
} sections[MAX_SECTIONS];

static void start_circuit(int c) {
	circuit = c;
	srand(SEED);
	g.circut = c;
	g.race_class = RACE_CLASS_VENOM;
	g.race_type = RACE_TYPE_SINGLE;
	g.pilot = 0;
	g.is_attract_mode = false;
	game_set_scene(GAME_SCENE_RACE);
	phase = PHASE_LOADING;
}

void bench_start(void) {
	bench_log("bench: one lap of %s, Venom class, AI driving, weapons off, fixed 1/60s tick",
		FIRST_CIRCUIT == LAST_CIRCUIT ? "track02" : "every circuit");
	start_circuit(FIRST_CIRCUIT);
}

// Its folder, such as "track02", to match the load: lines.
static const char *track_name(void) {
	static char name[16];
	const char *path = def.circuts[circuit].settings[RACE_CLASS_VENOM].path;
	const char *p = strstr(path, "track");
	snprintf(name, sizeof(name), "%.7s", p ? p : path);
	return name;
}

static void log_stats(const char *what) {
	stats_t *s = &stats;
	float ms = 1000.0f / PROF_HZ;
	uint32_t n = s->frames ? s->frames : 1;
	bench_log("bench: %s %s: %u frames, work %.2f ms avg, %.2f worst, render %.2f, %u polys, %.1f KB to GS; uncapped %.0f fps, %u frames over budget (%.1f%%)",
		track_name(), what, (unsigned)s->frames,
		(double)(s->work * ms / n), (double)(s->worst * ms), (double)(s->render * ms / n),
		(unsigned)(s->polys / n), (double)(s->qwords * 16 / 1024.0f / n),
		(double)(s->work ? PROF_HZ * n / s->work : 0), (unsigned)s->over,
		(double)(s->over * 100.0f / n));
	memset(s, 0, sizeof(*s));
}

// The lap's five slowest track sections (by the camera's section), by
// average work per frame.
static void log_sections(void) {
	char line[256];
	int len = snprintf(line, sizeof(line), "bench: %s slowest sections:", track_name());
	static uint8_t used[MAX_SECTIONS];
	memset(used, 0, sizeof(used));
	for (int k = 0; k < 5; k++) {
		int best = -1;
		uint64_t best_avg = 0;
		for (int i = 0; i < MAX_SECTIONS; i++) {
			if (!sections[i].frames || used[i]) {
				continue;
			}
			uint64_t avg = sections[i].work / sections[i].frames;
			if (best < 0 || avg > best_avg) {
				best = i;
				best_avg = avg;
			}
		}
		if (best < 0) {
			break;
		}
		used[best] = 1;
		len += snprintf(line + len, sizeof(line) - len, " %d (%.2f ms)", best,
			(double)(best_avg * 1000.0f / PROF_HZ));
	}
	bench_log("%s", line);
	memset(sections, 0, sizeof(sections));
}

void bench_update(void) {
	if (phase == PHASE_DONE) {
		return;
	}
	ship_t *ship = &g.ships[g.pilot];

	if (phase == PHASE_LOADING) {
		// The race scene's first frame has run: hand the ship to the AI
		// (it takes over at GO) and switch off the pickups, so nobody gets
		// weapons.
		if (g.camera.update_func != camera_update_race_intro) {
			return;
		}
		flags_rm(ship->flags, SHIP_RACING);
		for (int i = 0; i < g.track.pickups_len; i++) {
			flags_rm(g.track.pickups[i].face->flags, FACE_PICKUP_ACTIVE);
		}
		g.track.pickups_len = 0;
		memset(&stats, 0, sizeof(stats));
		phase = PHASE_INTRO;
		return;
	}

	// The intro hands over to the cockpit view; use the camera behind the
	// ship instead, which also draws it.
	if (g.camera.update_func == camera_update_race_internal) {
		g.camera.update_func = camera_update_race_external;
		flags_rm(ship->flags, SHIP_VIEW_INTERNAL);
	}

	if (phase == PHASE_INTRO && g.camera.update_func != camera_update_race_intro) {
		log_stats("intro");
		phase = PHASE_GRID;
	}
	else if (phase == PHASE_GRID && ship->update_func == ship_ai_update_race) {
		memset(&stats, 0, sizeof(stats));
		memset(sections, 0, sizeof(sections));
		phase = PHASE_LAP;
	}
#ifdef BENCH_FREEZE
	else if (phase == PHASE_LAP && stats.frames == BENCH_FREEZE) {
		// The game keeps drawing, but nothing moves.
		system_time_scale_set(0);
		printf("bench: frozen at lap frame %d\n", BENCH_FREEZE);
		phase = PHASE_DONE;
	}
#endif
	else if (phase == PHASE_LAP && (ship->lap >= 1 || stats.frames >= TIMEOUT_FRAMES)) {
		log_stats(ship->lap >= 1 ? "lap" : "lap TIMED OUT");
		log_sections();
		if (circuit < LAST_CIRCUIT) {
			start_circuit(circuit + 1);
		}
		else {
			bench_log("bench: done");
			phase = PHASE_DONE;
			write_results();
		}
	}
}

void bench_frame(uint32_t work, const prof_counters_t *counters) {
	if (phase != PHASE_INTRO && phase != PHASE_LAP) {
		return;
	}
	stats.frames++;
	stats.work += work;
	stats.render += counters->render;
	stats.polys += counters->polys;
	stats.qwords += counters->qwords;
	if (work > stats.worst) {
		stats.worst = work;
	}
	if (work > FRAME_CYCLES) {
		stats.over++;
	}
	if (phase == PHASE_LAP && g.camera.section) {
		int s = g.camera.section - g.track.sections;
		if (s >= 0 && s < MAX_SECTIONS) {
			sections[s].work += work;
			sections[s].frames++;
		}
	}
}

#endif
