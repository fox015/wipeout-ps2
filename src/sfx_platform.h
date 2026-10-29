#ifndef SFX_PLATFORM_H
#define SFX_PLATFORM_H

// Per-sound state owned by the platform's sound code (sfx_dc.c, sfx_ps2.c).
// Game code never looks inside it.

#if defined(_arch_dreamcast)
#include <dc/sound/sfxmgr.h>
typedef sfx_play_data_t sfx_platform_t;
#elif defined(_EE)
typedef struct {
	int voice;
} sfx_platform_t;
#else
#error "sfx_platform.h: no sound implementation for this platform"
#endif

#endif
