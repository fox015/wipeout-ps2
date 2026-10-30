// PS2 platform layer: startup, main loop, timing and file access.
// Saves are stubbed until stage 8.

#include <string.h>
#include <sio.h>
#include <timer.h>

#include "platform.h"
#include "system.h"
#include "utils.h"
#include "mem.h"
#include "todo_ps2.h"
#include "pad_ps2.h"

char *path_assets = "host:";
char *path_userdata = "host:wipeout/";
char *temp_path = NULL;

static volatile int wants_to_exit = 0;

// Set by the button remap menu so A/Start register only on press (stage 4).
int if_to_await = 0;

// stdout and stderr go to the serial port, which PCSX2 copies to emuLog.txt.
// Line buffered, so each line reaches the log in one piece.
static int sio_stream_write(void *cookie, const char *buf, int len) {
	return sio_write((void *)buf, len);
}

static void log_to_sio(void) {
	sio_init(115200, 0, 0, 0, 0);
	stdout = fwopen(NULL, sio_stream_write);
	stderr = fwopen(NULL, sio_stream_write);
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);
}

void ps2_log(const char *msg) {
	fputs(msg, stdout);
}

void ps2_todo(const char *func) {
	printf("TODO: %s\n", func);
}

void platform_exit(void) {
	wants_to_exit = 1;
}

vec2i_t platform_screen_size(void) {
	return vec2i(640, 480);
}

// Seconds since the first call, from the EE's bus-clock timer.
float platform_now(void) {
	static u64 start;
	u64 now = GetTimerSystemTime();
	if (!start) {
		start = now;
	}
	u32 sec, usec;
	TimerBusClock2USec(now - start, &sec, &usec);
	return (float)sec + (float)usec * 0.000001f;
}

bool platform_get_fullscreen(void) {
	return true;
}

void platform_set_fullscreen(bool fullscreen) {
}

void platform_set_audio_mix_cb(void (*cb)(float *buffer, uint32_t len)) {
	TODO();
}

FILE *platform_open_asset(const char *name, const char *mode) {
	char *path = strcat(strcpy(temp_path, path_assets), name);
	return fopen(path, mode);
}

uint8_t *platform_load_asset(const char *name, uint32_t *bytes_read) {
	char *path = strcat(strcpy(temp_path, path_assets), name);
	uint8_t *bytes = file_load(path, bytes_read);
	printf("load: %s (%u bytes)\n", name, (unsigned)*bytes_read);
	return bytes;
}

uint8_t *platform_load_userdata(const char *name, uint32_t *bytes_read) {
	TODO();
	*bytes_read = 0;
	return NULL;
}

uint32_t platform_store_userdata(const char *name, void *bytes, int32_t len) {
	TODO();
	return 0;
}

int main(int argc, char *argv[]) {
	log_to_sio();
	printf("wipeout-ps2: main\n");

	// Room for concatenating the asset and userdata paths with file names.
	temp_path = mem_bump(max(strlen(path_assets), strlen(path_userdata)) + 64);

	pad_init();
	system_init();

	// No display yet (stage 3), so log a frame count once a second instead.
	uint32_t frames = 0;
	float next_report = platform_now() + 1.0f;
	while (!wants_to_exit) {
		pad_update();
		system_update();
		frames++;
		float now = platform_now();
		if (now >= next_report) {
			printf("frames: %u (t=%.1fs)\n", (unsigned)frames, (double)now);
			next_report = now + 1.0f;
		}
	}

	system_cleanup();
	return 0;
}
