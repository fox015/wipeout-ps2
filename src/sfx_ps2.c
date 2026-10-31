// PS2 sound effects on the SPU2's hardware voices, through ps2snd (EE calls
// to libsd on the IOP).
//
// wipeout.vb is the PS1's own ADPCM, the format the SPU2 plays, so it goes
// into sound RAM as it is: no decoding. Each sound effect is a voice with its
// own pitch, volume and pan, as on the PS1. Reverb is core 0's hardware
// effect, faded in and out for tunnels. Music is still a stub.

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <kernel.h>
#include <timer.h>
#include <libsd.h>
#include <ps2snd.h>

#include "utils.h"
#include "mem.h"
#include "platform.h"
#include "system.h"
#include "wipeout/sfx.h"
#include "wipeout/game.h"
#include "todo_ps2.h"

extern int sfx_from_menu;

// Recently played music, so random play avoids repeats. The menus use it too.
volatile uint32_t last_five_tracks[5] = {-1, -1, -1, -1, -1};

// Sound RAM: below 0x5000 is the cores' input buffers; the samples go after.
#define SPU_SAMPLES_ADDR 0x5010

// Sound effects use core 0's 24 voices.
#define VOICES 24
#define CORE 0

// SPU2 volume 0x3fff is full scale; leave headroom for music on top.
#define VOLUME_SCALE 0x3000

// Reverb: the effect runs all the time and its return volume fades between
// 0 and REVERB_DEPTH over REVERB_FADE seconds.
#define REVERB_MODE  SD_EFFECT_MODE_HALL
#define REVERB_DEPTH 0x3000
#define REVERB_FADE  0.3f

// ADPCM block flags (the second byte of each 16-byte block)
#define BLOCK_LOOP_END    1 // last block: stop, or jump back if LOOP_REPEAT
#define BLOCK_LOOP_REPEAT 2
#define BLOCK_LOOP_START  4 // first block

typedef struct {
	uint32_t addr;     // in sound RAM
	uint32_t end_addr; // of the last block
	uint8_t looped;    // last block patched to jump back to the start
} source_t;

typedef struct {
	sfx_t *owner;      // the reserved loop on this voice, or NULL
	uint64_t started;  // for stealing the oldest one-shot
	// last values sent, to skip unchanged ones
	uint16_t voll, volr, pitch;
} voice_t;

static source_t *sources;
static uint32_t num_sources;
// wipeout.vb as uploaded, kept so a sample can be patched to loop
static uint8_t *samples;
static sfx_t *nodes;
static float reverb_level;   // 0 to 1
static uint16_t reverb_sent; // last EVOL sent
static bool reverb_on;
static voice_t voices[VOICES];
static uint64_t voice_clock;
static float unpause_sfx_volume;

// Voice parameter calls to the IOP, logged once a second.
static uint32_t stat_params;
static float stat_next;

void wav_volume(int volume) { TODO(); }

static void set_param(int voice, uint16_t param, uint16_t value, uint16_t *sent) {
	if (*sent != value) {
		sceSdSetParam(SD_VOICE(CORE, voice) | param, value);
		*sent = value;
		stat_params++;
	}
	float now = platform_now();
	if (now >= stat_next) {
		if (stat_params) {
			printf("sfx: %u voice parameter calls in the last second\n", (unsigned)stat_params);
		}
		stat_params = 0;
		stat_next = now + 1.0f;
	}
}

// Send a node's volume, pan and pitch to its voice. Pan as in wipeout-rewrite:
// each side fades from full at the centre to silent at the far side.
static void voice_update(int voice, sfx_t *sfx) {
	float volume = sfx->volume;
	if (sfx_from_menu && unpause_sfx_volume > 0.0f) {
		volume *= unpause_sfx_volume;
	} else {
		volume *= save.sfx_volume;
	}
	float left = volume * clamp(1.0f - sfx->pan, 0.0f, 1.0f);
	float right = volume * clamp(1.0f + sfx->pan, 0.0f, 1.0f);
	// pitch 1.0 is 44.1 kHz; the SPU2's 0x1000 is 48 kHz
	float pitch = clamp(sfx->pitch * (44100.0f / 48000.0f) * 4096.0f, 0.0f, 0x3fff);

	voice_t *v = &voices[voice];
	set_param(voice, SD_VPARAM_VOLL, (uint16_t)(clamp(left, 0.0f, 1.0f) * VOLUME_SCALE), &v->voll);
	set_param(voice, SD_VPARAM_VOLR, (uint16_t)(clamp(right, 0.0f, 1.0f) * VOLUME_SCALE), &v->volr);
	set_param(voice, SD_VPARAM_PITCH, (uint16_t)pitch, &v->pitch);
}

static void voice_start(int voice, sfx_t *sfx) {
	voices[voice].started = ++voice_clock;
	sfx->data.voice = voice;
	sceSdSetAddr(SD_VOICE(CORE, voice) | SD_VADDR_SSA, sources[sfx->source].addr);
	voice_update(voice, sfx);
	sceSdSetSwitch(CORE | SD_SWITCH_KON, 1 << voice);
}

// A voice that isn't a reserved loop: one that has finished, or else the
// one-shot that started longest ago.
static int voice_alloc_oneshot(void) {
	uint32_t ended = sceSdGetSwitch(CORE | SD_SWITCH_ENDX);
	int oldest = -1;
	for (int i = 0; i < VOICES; i++) {
		if (voices[i].owner) {
			continue;
		}
		if (ended & (1 << i)) {
			return i;
		}
		if (oldest < 0 || voices[i].started < voices[oldest].started) {
			oldest = i;
		}
	}
	return oldest;
}

static int voice_alloc_loop(sfx_t *owner) {
	int voice = voice_alloc_oneshot();
	if (voice >= 0) {
		voices[voice].owner = owner;
	}
	return voice;
}

// Make a source loop: its last block jumps back to its first (the voice sets
// its loop address at the LOOP_START block). The engine, crowd and other
// loops are only ever played as loops. Re-uploads the 64 bytes around the
// block.
static void source_set_looped(source_t *s) {
	if (s->looped) {
		return;
	}
	uint32_t offset = s->end_addr - SPU_SAMPLES_ADDR;
	samples[offset + 1] |= BLOCK_LOOP_REPEAT;
	uint32_t from = offset & ~63;
	FlushCache(0);
	sndLoadSample(samples + from, SPU_SAMPLES_ADDR + from, 64);
	s->looped = 1;
}

static void reverb_set_depth(uint16_t depth) {
	if (depth != reverb_sent) {
		sceSdSetParam(CORE | SD_PARAM_EVOLL, depth);
		sceSdSetParam(CORE | SD_PARAM_EVOLR, depth);
		reverb_sent = depth;
	}
}

void sfx_set_reverb(bool on) {
	if (on != reverb_on) {
		printf("sfx: reverb %s (section %d)\n", on ? "on" : "off", g.camera.section->num);
		reverb_on = on;
	}
	float step = system_tick() / REVERB_FADE;
	reverb_level = clamp(reverb_level + (on ? step : -step), 0.0f, 1.0f);
	reverb_set_depth(reverb_level * REVERB_DEPTH);
}

void sfx_load(void) {
	unpause_sfx_volume = 0.0f;

	nodes = mem_bump(SFX_MAX * sizeof(sfx_t));
	for (int i = 0; i < SFX_MAX; i++) {
		memset(&nodes[i], 0, sizeof(sfx_t));
		nodes[i].chn = -1;
		nodes[i].data.voice = -1;
	}

	sceSdInit(0);
	for (int core = 0; core < 2; core++) {
		sceSdSetParam(core | SD_PARAM_MVOLL, 0x3fff);
		sceSdSetParam(core | SD_PARAM_MVOLR, 0x3fff);
	}
	sceSdSetSwitch(CORE | SD_SWITCH_VMIXL, 0xffffff);
	sceSdSetSwitch(CORE | SD_SWITCH_VMIXR, 0xffffff);
	for (int i = 0; i < VOICES; i++) {
		// Full volume at once, held until key off, then a quick release.
		sceSdSetParam(SD_VOICE(CORE, i) | SD_VPARAM_ADSR1, SD_SET_ADSR1(SD_ADSR_AR_LINEARi, 0, 0, 0xf));
		sceSdSetParam(SD_VOICE(CORE, i) | SD_VPARAM_ADSR2, SD_SET_ADSR2(SD_ADSR_SR_LINEARi, 0x7f, SD_ADSR_RR_LINEARd, 0x8));
		voices[i] = (voice_t){.voll = 0xffff, .volr = 0xffff, .pitch = 0xffff};
	}

	// The effect's work area is at the top of sound RAM, well above the samples.
	sceSdEffectAttr fx = {.core = CORE, .mode = REVERB_MODE | SD_EFFECT_MODE_CLEAR};
	sceSdSetEffectAttr(CORE, &fx);
	sceSdSetCoreAttr(CORE | SD_CORE_EFFECT_ENABLE, 1);
	sceSdSetSwitch(CORE | SD_SWITCH_VMIXEL, 0xffffff);
	sceSdSetSwitch(CORE | SD_SWITCH_VMIXER, 0xffffff);
	reverb_set_depth(0);
	printf("sfx: reverb work area 0x%x-0x%x, mixer 0x%x\n",
		(unsigned)sceSdGetAddr(CORE | SD_ADDR_ESA), (unsigned)sceSdGetAddr(CORE | SD_ADDR_EEA),
		(unsigned)sceSdGetParam(CORE | SD_PARAM_MMIX));

	uint32_t vb_size;
	uint8_t *vb = platform_load_asset("wipeout/sound/wipeout.vb", &vb_size);

	// Find each sample: it starts at a LOOP_START block and ends at the next
	// LOOP_END block.
	sources = mem_mark();
	num_sources = 0;
	uint32_t start = 0;
	for (uint32_t p = 0; p + 16 <= vb_size; p += 16) {
		uint8_t flags = vb[p + 1];
		if (flags & BLOCK_LOOP_START) {
			start = p;
		}
		if (flags & BLOCK_LOOP_END) {
			mem_bump(sizeof(source_t));
			sources[num_sources++] = (source_t){
				.addr = SPU_SAMPLES_ADDR + start,
				.end_addr = SPU_SAMPLES_ADDR + p,
			};
		}
	}

	// ps2snd sends the samples from EE memory, aligned; the copy stays for
	// source_set_looped().
	samples = memalign(64, (vb_size + 63) & ~63);
	memcpy(samples, vb, vb_size);
	mem_temp_free(vb);
	FlushCache(0);
	int sent = sndLoadSample(samples, SPU_SAMPLES_ADDR, vb_size);

	// What a voice parameter costs: each is a call to the IOP.
	u32 sec, usec;
	u64 t0 = GetTimerSystemTime();
	for (int i = 0; i < 16; i++) {
		sceSdSetParam(SD_VOICE(CORE, 0) | SD_VPARAM_VOLL, 0);
	}
	TimerBusClock2USec(GetTimerSystemTime() - t0, &sec, &usec);

	printf("sfx: %u samples, %u bytes to sound RAM (sent %d), %u us per voice parameter\n",
		(unsigned)num_sources, (unsigned)vb_size, sent, (unsigned)(sec * 1000000 + usec) / 16);
}

void sfx_stero_mix(float *buffer, uint32_t len) {}
void sfx_set_external_mix_cb(void (*cb)(float *, uint32_t len)) {}

void sfx_reset(void) {
	reverb_on = false;
	reverb_level = 0;
	reverb_set_depth(0);

	for (int i = 0; i < SFX_MAX; i++) {
		sfx_t *sfx = &nodes[i];
		if (flags_is(sfx->flags, SFX_LOOP)) {
			int voice = sfx->data.voice;
			if (voice >= 0) {
				sceSdSetSwitch(CORE | SD_SWITCH_KOFF, 1 << voice);
				voices[voice].owner = NULL;
			}
			sfx->data.voice = -1;
			flags_set(sfx->flags, SFX_NONE);
		}
	}

	if (save.sfx_volume == 0.0f && unpause_sfx_volume > 0.0f) {
		save.sfx_volume = unpause_sfx_volume;
		unpause_sfx_volume = 0.0f;
	}
}

// Pausing mutes sound effects by zeroing the setting, as on the Dreamcast.
void sfx_pause(void) {
	unpause_sfx_volume = save.sfx_volume;
	save.sfx_volume = 0;
	for (int i = 0; i < SFX_MAX; i++) {
		if (flags_any(nodes[i].flags, SFX_RESERVE)) {
			sfx_update_ex(&nodes[i]);
		}
		if (flags_is(nodes[i].flags, SFX_LOOP)) {
			flags_add(nodes[i].flags, SFX_LOOP_PAUSE);
		}
	}
}

void sfx_unpause(void) {
	if (save.sfx_volume == 0.0f && unpause_sfx_volume > 0.0f) {
		save.sfx_volume = unpause_sfx_volume;
		unpause_sfx_volume = 0.0f;
	}
	for (int i = 0; i < SFX_MAX; i++) {
		if (flags_any(nodes[i].flags, SFX_RESERVE)) {
			sfx_update_ex(&nodes[i]);
		}
		if (flags_is(nodes[i].flags, SFX_LOOP_PAUSE)) {
			flags_rm(nodes[i].flags, SFX_LOOP_PAUSE);
		}
	}
}

static sfx_t *sfx_get_node(sfx_source_t source_index) {
	error_if(source_index < 0 || source_index >= num_sources, "Invalid audio source");

	sfx_t *sfx = NULL;
	for (int i = 0; i < SFX_MAX; i++) {
		if (flags_none(nodes[i].flags, SFX_RESERVE)) {
			sfx = &nodes[i];
			break;
		}
	}
	error_if(!sfx, "All audio nodes reserved");

	flags_set(sfx->flags, SFX_NONE);
	sfx->source = source_index;
	sfx->volume = 1;
	sfx->current_volume = 1;
	sfx->pan = 0;
	sfx->current_pan = 0;
	sfx->position = 0;
	sfx->data.voice = -1;

	// All voice samples are 44 kHz, other effects 22 kHz
	sfx->pitch = source_index >= SFX_VOICE_MINES ? 1.0f : 0.5f;
	return sfx;
}

sfx_t *sfx_play(sfx_source_t source_index) {
	sfx_t *sfx = sfx_get_node(source_index);
	int voice = voice_alloc_oneshot();
	if (voice >= 0) {
		voice_start(voice, sfx);
	}
	return sfx;
}

// Volume by distance, pan by direction and pitch by Doppler shift, relative
// to the camera, as on the Dreamcast.
static void sfx_position(sfx_t *sfx, vec3_t pos, vec3_t vel, float volume) {
	vec3_t relative_position = vec3_sub(g.camera.position, pos);
	vec3_t relative_velocity = vec3_sub(g.camera.real_velocity, vel);
	float distance = vec3_len(relative_position);

	sfx->volume = clamp(scale(distance, 512, 32768, 1, 0), 0, 1) * volume;
	sfx->pan = -sinf(bump_atan2f(g.camera.position.x - pos.x, g.camera.position.z - pos.z) + g.camera.angle.y);

	float away = vec3_dot(relative_velocity, relative_position) * approx_recip(distance);
	sfx->pitch = (262144.0f - away) * (1.0f / 524288.0f);
}

sfx_t *sfx_play_at(sfx_source_t source_index, vec3_t pos, vec3_t vel, float volume) {
	sfx_t *sfx = sfx_get_node(source_index);
	sfx_position(sfx, pos, vel, volume);
	if (sfx->volume > 0) {
		int voice = voice_alloc_oneshot();
		if (voice >= 0) {
			voice_start(voice, sfx);
		}
	}
	return sfx;
}

sfx_t *sfx_reserve_loop(sfx_source_t source_index) {
	sfx_t *sfx = sfx_get_node(source_index);
	flags_set(sfx->flags, SFX_RESERVE | SFX_LOOP);
	sfx->volume = 0;
	sfx->current_volume = 0;

	source_set_looped(&sources[source_index]);
	int voice = voice_alloc_loop(sfx);
	if (voice >= 0) {
		voice_start(voice, sfx);
	}
	return sfx;
}

void sfx_update_ex(sfx_t *sfx) {
	if (sfx->data.voice >= 0) {
		voice_update(sfx->data.voice, sfx);
	}
}

void sfx_set_position(sfx_t *sfx, vec3_t pos, vec3_t vel, float volume) {
	sfx_position(sfx, pos, vel, volume);
	sfx_update_ex(sfx);
}

void sfx_music_next(void) { TODO(); }
void sfx_music_play(uint32_t index) { TODO(); }
void sfx_music_mode(sfx_music_mode_t mode) { TODO(); }
void sfx_music_pause(void) { TODO(); }
