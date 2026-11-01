// PS2 sound effects on the SPU2's hardware voices, through ps2snd (EE calls
// to libsd on the IOP).
//
// wipeout.vb is the PS1's own ADPCM, the format the SPU2 plays, so it goes
// into sound RAM as it is: no decoding. Each sound effect is a voice with its
// own pitch, volume and pan, as on the PS1. Reverb is core 1's hardware
// effect, faded in and out for tunnels; it takes the music too.
//
// Music is QOA, read from the host as it plays (122 MB doesn't fit in RAM),
// decoded on the EE and streamed to audsrv as 16-bit PCM by its own thread.

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <kernel.h>
#include <timer.h>
#include <libsd.h>
#include <ps2snd.h>
#include <audsrv.h>
#include <sifrpc.h>

#define QOA_IMPLEMENTATION
#define QOA_NO_STDIO
#include "qoa.h"

#include "utils.h"
#include "mem.h"
#include "platform.h"
#include "system.h"
#include "wipeout/sfx.h"
#include "wipeout/game.h"
#include "sfx_ps2.h"

extern int sfx_from_menu;
extern char *path_assets;
extern void *_gp;

static volatile int music_volume;
static void music_init(void);

// Recently played music, so random play avoids repeats. The menus use it too.
volatile uint32_t last_five_tracks[5] = {-1, -1, -1, -1, -1};

// Sound RAM: below 0x5000 is the cores' input buffers; the samples go after.
#define SPU_SAMPLES_ADDR 0x5010

// Sound effects use core 1's 24 voices. audsrv mutes core 0 (its master
// volume goes to 0 whenever the music volume changes) and streams the music
// into core 1, whose voices it only uses for its own ADPCM playback (unused).
#define VOICES 24
#define CORE 1

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

// Voice parameter changes, and the batches that carried them to the IOP,
// logged once a second.
static uint32_t stat_params, stat_batches;
static float stat_next;

// Per-frame commands (voice volumes and pitch, key on and off, reverb volume)
// go to the IOP in batches: ps2snd's server runs libsd's sceSdProcBatch() on
// them (ps2snd's EE library only has a stub for it). That's one RPC for up to
// BATCH_MAX commands instead of a blocking RPC, about 50us, for each. The RPC
// doesn't wait for the IOP; the server has a single receive buffer, so the
// next batch, and any other ps2snd call, waits for it to finish first.
#define BATCH_MAX 15 // the server's buffer is 128 bytes: a count, then 8 a command

static SifRpcClientData_t batch_client;
static struct {
	uint32_t count;
	sceSdBatch command[BATCH_MAX];
} batch_sent __attribute__((aligned(64)));
static sceSdBatch batch[BATCH_MAX];
static int batch_len;

static void batch_wait(void) {
	while (sceSifCheckStatRpc(&batch_client)) {
	}
}

static void batch_send(void) {
	if (batch_len == 0) {
		return;
	}
	batch_wait();
	batch_sent.count = batch_len;
	memcpy(batch_sent.command, batch, batch_len * sizeof(sceSdBatch));
	sceSifCallRpc(&batch_client, PS2SND_ProcBatch, SIF_RPC_M_NOWAIT, &batch_sent,
		sizeof(uint32_t) + batch_len * sizeof(sceSdBatch), NULL, 0, NULL, NULL);
	batch_len = 0;
	stat_batches++;
}

static void batch_add(uint16_t func, uint16_t entry, uint32_t value) {
	if (batch_len == BATCH_MAX) {
		batch_send();
	}
	batch[batch_len++] = (sceSdBatch){.func = func, .entry = entry, .value = value};
}

// Before calling ps2snd directly: everything queued has reached libsd.
static void batch_sync(void) {
	batch_send();
	batch_wait();
}

void sfx_ps2_frame_end(void) {
	batch_send();
}

// Music volume as the Dreamcast's stream volume, 0-255. The menu sets it;
// sfx_music_pause() mutes with 0 until the next track starts.
void wav_volume(int volume) {
	music_volume = clamp(volume, 0, 255);
}

static void set_param(int voice, uint16_t param, uint16_t value, uint16_t *sent) {
	if (*sent != value) {
		batch_add(SD_BATCH_SETPARAM, SD_VOICE(CORE, voice) | param, value);
		*sent = value;
		stat_params++;
	}
	float now = platform_now();
	if (now >= stat_next) {
		if (stat_params) {
			printf("sfx: %u voice parameter changes in %u batches in the last second\n",
				(unsigned)stat_params, (unsigned)stat_batches);
		}
		stat_params = stat_batches = 0;
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
	batch_add(SD_BATCH_SETADDR, SD_VOICE(CORE, voice) | SD_VADDR_SSA, sources[sfx->source].addr);
	voice_update(voice, sfx);
	batch_add(SD_BATCH_SETSWITCH, CORE | SD_SWITCH_KON, 1 << voice);
}

// A voice that isn't a reserved loop: one that has finished, or else the
// one-shot that started longest ago.
static int voice_alloc_oneshot(void) {
	// Key ons still queued would leave their voices marked as ended.
	batch_sync();
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
	batch_sync();
	FlushCache(0);
	sndLoadSample(samples + from, SPU_SAMPLES_ADDR + from, 64);
	s->looped = 1;
}

static void reverb_set_depth(uint16_t depth) {
	if (depth != reverb_sent) {
		batch_add(SD_BATCH_SETPARAM, CORE | SD_PARAM_EVOLL, depth);
		batch_add(SD_BATCH_SETPARAM, CORE | SD_PARAM_EVOLR, depth);
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

	// Each libsd init resets every voice, volume and transfer. ps2snd's
	// sceSdInit() connects its RPC client and initialises libsd; then
	// audsrv_init() initialises it again and starts the music stream into
	// core 1. Our settings go on top of that.
	sceSdInit(0);
	while (sceSifBindRpc(&batch_client, BINDID_PS2SND, 0) < 0 || !batch_client.server) {
	}
	audsrv_init();
	sceSdSetParam(CORE | SD_PARAM_MVOLL, 0x3fff);
	sceSdSetParam(CORE | SD_PARAM_MVOLR, 0x3fff);
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
	batch_sync();
	printf("sfx: reverb work area 0x%x-0x%x, mixer 0x%x\n",
		(unsigned)sceSdGetAddr(CORE | SD_ADDR_ESA), (unsigned)sceSdGetAddr(CORE | SD_ADDR_EEA),
		(unsigned)sceSdGetParam(CORE | SD_PARAM_MMIX));

	music_init();

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
				batch_add(SD_BATCH_SETSWITCH, CORE | SD_SWITCH_KOFF, 1 << voice);
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

// Music. The main thread only sets these; the music thread does all the
// file reading, decoding and audsrv calls, so the game never waits on them.

#define MUSIC_READ_SIZE (64 * 1024)
#define MUSIC_CHUNK_FRAMES 1020 // stereo sample pairs per audsrv call, 23ms: 51 QOA slices

static volatile uint32_t music_track_index = -1;
static volatile sfx_music_mode_t music_mode = SFX_MUSIC_RANDOM;
static volatile int music_interrupt; // a new track was asked for
static volatile int music_skip;      // end this track as if it finished
static int music_start_sema = -1;

static uint8_t music_read_buf[MUSIC_READ_SIZE] __attribute__((aligned(64)));
static short music_pcm[MUSIC_CHUNK_FRAMES * 2] __attribute__((aligned(64)));
static uint8_t music_thread_stack[16 * 1024] __attribute__((aligned(16)));

static __attribute__((unused)) bool music_track_recent(uint32_t index) {
	for (int i = 0; i < 5; i++) {
		if (last_five_tracks[i] == index) {
			return true;
		}
	}
	return false;
}

// What plays after a track finishes, as in sfx_dc.c.
static void music_pick_next(void) {
	if (music_mode == SFX_MUSIC_SCENECHANGE) {
		music_mode = SFX_MUSIC_RANDOM;
	}
	if (music_mode == SFX_MUSIC_RANDOM) {
		uint32_t index;
#ifndef BENCH
		do {
			index = rand_int(0, len(def.music));
		} while (music_track_recent(index));
#else
		// This thread runs on real time: a rand() here would shift the
		// benchmark race's random numbers.
		index = (music_track_index + 1) % len(def.music);
#endif
		for (int i = 1; i < 5; i++) {
			last_five_tracks[i - 1] = last_five_tracks[i];
		}
		last_five_tracks[4] = index;
		music_track_index = index;
	}
	else if (music_mode == SFX_MUSIC_SEQUENTIAL) {
		music_track_index = (music_track_index + 1) % len(def.music);
	}
	// SFX_MUSIC_LOOP and SFX_MUSIC_PAUSED play the same track again.
}

// Plays one track until it ends or another is asked for.
// A QOA frame (5120 samples, 116 ms) took about 4.6 ms to decode in PCSX2,
// all at once, and the music thread runs one priority above the game, so
// every seventh frame or so went over budget. Decoding a chunk at a time, just
// before audsrv needs it, spreads the same work over the frames that play it.
// These do what qoa_decode_frame() does, in pieces, with qoa.h's own LMS
// predictor and tables.
typedef struct {
	const unsigned char *bytes;
	unsigned int p;       // the next slice
	unsigned int samples; // per channel, in this frame
	unsigned int decoded; // per channel, so far
} music_frame_t;

// Read a frame's header and LMS state. Returns the frame's size in bytes, or
// 0 if there's no valid frame (the end of the track).
static unsigned int music_frame_begin(music_frame_t *fr, const unsigned char *bytes, unsigned int size,
		qoa_desc *qoa) {
	if (size < 8 + QOA_LMS_LEN * 4 * qoa->channels) {
		return 0;
	}
	unsigned int p = 0;
	qoa_uint64_t frame_header = qoa_read_u64(bytes, &p);
	unsigned int channels   = (frame_header >> 56) & 0x0000ff;
	unsigned int samplerate = (frame_header >> 32) & 0xffffff;
	unsigned int samples    = (frame_header >> 16) & 0x00ffff;
	unsigned int frame_size = (frame_header      ) & 0x00ffff;
	unsigned int num_slices = (frame_size - 8 - QOA_LMS_LEN * 4 * channels) / 8;
	if (
		channels != qoa->channels || samplerate != qoa->samplerate || frame_size > size ||
		samples == 0 || samples * channels > num_slices * QOA_SLICE_LEN
	) {
		return 0;
	}
	for (unsigned int c = 0; c < channels; c++) {
		qoa_uint64_t history = qoa_read_u64(bytes, &p);
		qoa_uint64_t weights = qoa_read_u64(bytes, &p);
		for (int i = 0; i < QOA_LMS_LEN; i++) {
			qoa->lms[c].history[i] = (signed short)(history >> 48);
			history <<= 16;
			qoa->lms[c].weights[i] = (signed short)(weights >> 48);
			weights <<= 16;
		}
	}
	*fr = (music_frame_t){.bytes = bytes, .p = p, .samples = samples, .decoded = 0};
	return frame_size;
}

// Decode the frame's next max samples per channel (a multiple of the slice
// length) into pcm, interleaved. Returns how many it decoded.
static unsigned int music_frame_decode(music_frame_t *fr, qoa_desc *qoa, short *pcm, unsigned int max) {
	unsigned int channels = qoa->channels;
	unsigned int start = fr->decoded;
	unsigned int end = min(start + max, fr->samples);
	for (unsigned int sample_index = start; sample_index < end; sample_index += QOA_SLICE_LEN) {
		unsigned int slice_len = min(QOA_SLICE_LEN, fr->samples - sample_index);
		for (unsigned int c = 0; c < channels; c++) {
			qoa_uint64_t slice = qoa_read_u64(fr->bytes, &fr->p);
			int scalefactor = (slice >> 60) & 0xf;
			slice <<= 4;
			short *out = pcm + (sample_index - start) * channels + c;
			for (unsigned int i = 0; i < slice_len; i++) {
				int predicted = qoa_lms_predict(&qoa->lms[c]);
				int quantized = (slice >> 61) & 0x7;
				int dequantized = qoa_dequant_tab[scalefactor][quantized];
				int reconstructed = qoa_clamp_s16(predicted + dequantized);
				out[i * channels] = reconstructed;
				slice <<= 3;
				qoa_lms_update(&qoa->lms[c], reconstructed, dequantized);
			}
		}
	}
	fr->decoded = end;
	return end - start;
}

static void music_play_track(uint32_t index) {
	// The game's paths are the Dreamcast's .adpcm files; ours are .qoa.
	char path[64];
	snprintf(path, sizeof(path), "%s%s", path_assets, def.music[index].path);
	char *ext = strrchr(path, '.');
	if (ext) {
		strcpy(ext, ".qoa");
	}

	FILE *f = fopen(path, "rb");
	if (!f) {
		printf("music: could not open %s\n", path);
		return;
	}
	printf("music: %s\n", path);

	uint32_t have = fread(music_read_buf, 1, MUSIC_READ_SIZE, f);
	qoa_desc qoa;
	uint32_t pos = qoa_decode_header(music_read_buf, have, &qoa);
	if (!pos || qoa.channels != 2) {
		printf("music: %s is not stereo QOA\n", path);
		fclose(f);
		return;
	}

	int volume_sent = -1;
	while (!music_interrupt && !music_skip) {
		// Keep at least one whole frame in the buffer.
		if (have - pos < QOA_FRAME_SIZE(2, QOA_SLICES_PER_FRAME)) {
			memmove(music_read_buf, music_read_buf + pos, have - pos);
			have -= pos;
			pos = 0;
			have += fread(music_read_buf + have, 1, MUSIC_READ_SIZE - have, f);
		}

		music_frame_t frame;
		uint32_t used = music_frame_begin(&frame, music_read_buf + pos, have - pos, &qoa);
		if (!used) {
			break; // end of the track
		}
		pos += used;

		while (frame.decoded < frame.samples && !music_interrupt) {
			// audsrv's volume is 0-100.
			int volume = music_volume * 100 / 255;
			if (volume != volume_sent) {
				audsrv_set_volume(volume);
				volume_sent = volume;
			}
			uint32_t frames = music_frame_decode(&frame, &qoa, music_pcm, MUSIC_CHUNK_FRAMES);
			int bytes = frames * 2 * sizeof(short);
			audsrv_wait_audio(bytes);
			audsrv_play_audio((char *)music_pcm, bytes);
		}
	}
	fclose(f);
}

static void music_thread(void *arg) {
	WaitSema(music_start_sema); // the first sfx_music_play()
	while (1) {
		music_interrupt = 0;
		music_skip = 0;
		music_volume = 192 * save.music_volume; // as the Dreamcast's wav_create()
		music_play_track(music_track_index);
		if (!music_interrupt) {
			music_pick_next();
		}
	}
}

static void music_init(void) {
	audsrv_fmt_t fmt = {.freq = 44100, .bits = 16, .channels = 2};
	audsrv_set_format(&fmt);
	printf("music: audsrv ready, %d bytes of buffer\n", audsrv_available());

	ee_sema_t sema = {.init_count = 0, .max_count = 1};
	music_start_sema = CreateSema(&sema);

	// One step above the game, so it runs whenever audsrv has room; it
	// sleeps in audsrv_wait_audio() the rest of the time.
	ee_thread_status_t status;
	ReferThreadStatus(GetThreadId(), &status);
	ee_thread_t thread = {
		.func = music_thread,
		.stack = music_thread_stack,
		.stack_size = sizeof(music_thread_stack),
		.gp_reg = &_gp,
		.initial_priority = status.current_priority - 1,
	};
	StartThread(CreateThread(&thread), NULL);
}

void sfx_music_next(void) {
	music_skip = 1;
}

void sfx_music_play(uint32_t index) {
	music_track_index = index;
	music_interrupt = 1;
	SignalSema(music_start_sema);
}

void sfx_music_mode(sfx_music_mode_t mode) {
	music_mode = mode;
}

void sfx_music_pause(void) {
	wav_volume(0);
}
