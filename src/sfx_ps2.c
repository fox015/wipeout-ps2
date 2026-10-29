// PS2 sound: stubs until stage 7 (audsrv).

#include "wipeout/sfx.h"
#include "todo_ps2.h"

// Handed out by sfx_play and friends, so callers always get a valid sfx_t.
static sfx_t dummy_sfx;

// Recently played music, so random play avoids repeats. The menus use it too.
volatile uint32_t last_five_tracks[5] = {-1, -1, -1, -1, -1};

void wav_volume(int volume) { TODO(); }

void sfx_load(void) { TODO(); }
void sfx_stero_mix(float *buffer, uint32_t len) { TODO(); }
void sfx_set_external_mix_cb(void (*cb)(float *, uint32_t len)) { TODO(); }
void sfx_reset(void) { TODO(); }
void sfx_pause(void) { TODO(); }
void sfx_unpause(void) { TODO(); }

sfx_t *sfx_play(sfx_source_t source_index) { TODO(); return &dummy_sfx; }
sfx_t *sfx_play_at(sfx_source_t source_index, vec3_t pos, vec3_t vel, float volume) { TODO(); return &dummy_sfx; }
sfx_t *sfx_reserve_loop(sfx_source_t source_index) { TODO(); return &dummy_sfx; }
void sfx_set_position(sfx_t *sfx, vec3_t pos, vec3_t vel, float volume) { TODO(); }
void sfx_update_ex(sfx_t *sfx) { TODO(); }

void sfx_music_next(void) { TODO(); }
void sfx_music_play(uint32_t index) { TODO(); }
void sfx_music_mode(sfx_music_mode_t mode) { TODO(); }
void sfx_music_pause(void) { TODO(); }
