// Saves on the memory card in slot 1, through libmc.
//
// The game stores one file, its settings and records, whenever it changes
// scene with something new to save (game_set_scene()). It lives in the save
// folder SAVE_DIR. Each libmc call is asynchronous on the IOP; mc_wait()
// waits for it, so a save holds up the frame it happens in.

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <libmc.h>

#include "mem.h"
#include "platform.h"

#define PORT 0 // slot 1
#define SLOT 0
#define SAVE_DIR "/WIPEOUT-PS2"

// The game's save data is a few hundred bytes; anything bigger isn't ours.
#define MAX_FILE_SIZE 8192

static int mc_wait(void) {
	int result;
	mcSync(MC_WAIT, NULL, &result);
	return result;
}

// Is there a formatted PS2 memory card in slot 1?
static int card_ready(const char *what) {
	int type, free, format;
	mcGetInfo(PORT, SLOT, &type, &free, &format);
	int result = mc_wait();
	// 0: the same card as last time; -1: another formatted card.
	if (result < -1 || type != MC_TYPE_PS2 || !format) {
		printf("memcard: no formatted PS2 memory card in slot 1 (%d), %s\n", result, what);
		return 0;
	}
	return 1;
}

static void file_path(char *path, const char *name) {
	snprintf(path, 64, "%s/%s", SAVE_DIR, name);
}

uint8_t *platform_load_userdata(const char *name, uint32_t *bytes_read) {
	*bytes_read = 0;
	if (!card_ready("not loading")) {
		return NULL;
	}

	char path[64];
	file_path(path, name);
	mcOpen(PORT, SLOT, path, sceMcFileAttrReadable);
	int fd = mc_wait();
	if (fd < 0) {
		printf("memcard: no %s (%d)\n", path, fd);
		return NULL;
	}

	mcSeek(fd, 0, SEEK_END);
	int size = mc_wait();
	mcSeek(fd, 0, SEEK_SET);
	mc_wait();

	uint8_t *bytes = NULL;
	if (size > 0 && size <= MAX_FILE_SIZE) {
		// libmc transfers to and from 64-byte aligned buffers.
		uint8_t *buf = memalign(64, size);
		mcRead(fd, buf, size);
		int read = mc_wait();
		if (read == size) {
			bytes = mem_temp_alloc(size);
			memcpy(bytes, buf, size);
			*bytes_read = size;
		}
		free(buf);
		printf("memcard: load %s (%d of %d bytes)\n", path, read, size);
	}
	else {
		printf("memcard: %s has an unexpected size (%d)\n", path, size);
	}

	mcClose(fd);
	mc_wait();
	return bytes;
}

uint32_t platform_store_userdata(const char *name, void *bytes, int32_t len) {
	if (!card_ready("not saving")) {
		return 0;
	}

	mcMkDir(PORT, SLOT, SAVE_DIR);
	int result = mc_wait();
	if (result < 0 && result != sceMcResNoEntry) { // -4: it already exists
		printf("memcard: could not make %s (%d)\n", SAVE_DIR, result);
		return 0;
	}

	char path[64];
	file_path(path, name);
	mcOpen(PORT, SLOT, path, sceMcFileAttrWriteable | sceMcFileCreateFile);
	int fd = mc_wait();
	if (fd < 0) {
		printf("memcard: could not open %s (%d)\n", path, fd);
		return 0;
	}

	uint8_t *buf = memalign(64, len);
	memcpy(buf, bytes, len);
	mcWrite(fd, buf, len);
	int written = mc_wait();
	free(buf);

	mcClose(fd);
	mc_wait();
	printf("memcard: save %s (%d of %d bytes)\n", path, written, (int)len);
	return written == len ? len : 0;
}
