#include <stdio.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <sbv_patches.h>

#include "iop_ps2.h"

// The .irx files from ps2sdk, turned into arrays by bin2c (see Makefile).
extern unsigned char libsd_irx[];
extern unsigned int size_libsd_irx;
extern unsigned char audsrv_irx[];
extern unsigned int size_audsrv_irx;
extern unsigned char ps2snd_irx[];
extern unsigned int size_ps2snd_irx;

static int load(const char *name, void *irx, unsigned int size) {
	int ret = 0;
	int id = SifExecModuleBuffer(irx, size, 0, NULL, &ret);
	if (id < 0 || ret == 1) { // 1: the module asked to be unloaded
		printf("iop: could not load %s (id %d, ret %d)\n", name, id, ret);
		return -1;
	}
	printf("iop: loaded %s\n", name);
	return 0;
}

int iop_load_sound_modules(void) {
	SifInitRpc(0);
	// Older BIOSes can't load modules from EE memory without this.
	sbv_patch_enable_lmb();

	if (load("libsd", libsd_irx, size_libsd_irx) < 0 ||
		load("audsrv", audsrv_irx, size_audsrv_irx) < 0 ||
		load("ps2snd", ps2snd_irx, size_ps2snd_irx) < 0) {
		return -1;
	}
	return 0;
}
