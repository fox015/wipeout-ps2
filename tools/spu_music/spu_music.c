// Converts a QOA music track into SPU2 ADPCM, laid out for ps2snd's IOP-side
// streaming (sndStreamOpen() and friends), so the PS2 plays music with no EE
// work: the IOP reads the file and the SPU2 decodes it in hardware.
//
// Usage: spu_music <input.qoa> <output.spu>
// Built and run by tools/convert_music.sh. It prints the signal-to-noise
// ratio of the result against the QOA track.
//
// SPU2 ADPCM (as on the PS1, see psx-spx): 16-byte blocks of 28 samples. Byte
// 0 is the shift (low nibble) and filter (high nibble), byte 1 flags, then 28
// signed 4-bit residuals, low nibble first. A sample decodes as
//   (residual << 12 >> shift) + (old * pos[filter] + older * neg[filter] + 32 >> 6)
// clamped to 16 bits. Each block here is encoded by trying every filter and
// shift with that decoder in the loop and keeping the closest.
//
// ps2snd's stream layout: CHUNK_BLOCKS blocks of the left channel, then the
// same of the right, and so on (the game's sfx_ps2.c uses the same chunk
// size). sndStreamOpen() reads the channel count and pitch from the file's
// first 12 bytes, then plays the file from its start, so those bytes are the
// left channel's first block: one that decodes to near-silence (shift 12). The
// right channel's first block is silent to match. The stream stops as soon as
// a chunk can't be read whole, while the previous one is still playing, so
// the file ends with a whole silent chunk.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QOA_IMPLEMENTATION
#include "../../src/qoa.h"

#define CHUNK_BLOCKS 1024
#define BLOCK_SAMPLES 28
#define BLOCK_BYTES 16

static const int filter_pos[5] = {0, 60, 115, 98, 122};
static const int filter_neg[5] = {0, 0, -52, -55, -60};

typedef struct {
	int old, older;
} adpcm_state_t;

static int clamp16(int v) {
	return v < -32768 ? -32768 : v > 32767 ? 32767 : v;
}

// Encode 28 samples (taken every stride shorts from x) into one block.
static void encode_block(const short *x, int stride, adpcm_state_t *state, uint8_t *out) {
	int64_t best_error = INT64_MAX;
	int best_filter = 0, best_shift = 12;
	int8_t best_nibbles[BLOCK_SAMPLES];
	adpcm_state_t best_state = *state;

	for (int filter = 0; filter < 5; filter++) {
		for (int shift = 0; shift <= 12; shift++) {
			int step = 1 << (12 - shift);
			int old = state->old, older = state->older;
			int64_t error = 0;
			int8_t nibbles[BLOCK_SAMPLES];
			int i;
			for (i = 0; i < BLOCK_SAMPLES; i++) {
				int sample = x[i * stride];
				int predicted = (old * filter_pos[filter] + older * filter_neg[filter] + 32) >> 6;
				int n = (int)floor((double)(sample - predicted) / step + 0.5);
				n = n < -8 ? -8 : n > 7 ? 7 : n;
				int decoded = clamp16(n * step + predicted);
				int64_t e = sample - decoded;
				error += e * e;
				if (error >= best_error) {
					break;
				}
				nibbles[i] = n;
				older = old;
				old = decoded;
			}
			if (i == BLOCK_SAMPLES) {
				best_error = error;
				best_filter = filter;
				best_shift = shift;
				memcpy(best_nibbles, nibbles, sizeof(nibbles));
				best_state = (adpcm_state_t){old, older};
			}
		}
	}

	memset(out, 0, BLOCK_BYTES);
	out[0] = best_shift | best_filter << 4;
	for (int i = 0; i < BLOCK_SAMPLES; i++) {
		out[2 + i / 2] |= (best_nibbles[i] & 0xf) << (4 * (i & 1));
	}
	*state = best_state;
}

// Decode one block, as the SPU2 does, for the quality check.
static void decode_block(const uint8_t *in, adpcm_state_t *state, short *out) {
	int shift = in[0] & 0xf, filter = in[0] >> 4;
	for (int i = 0; i < BLOCK_SAMPLES; i++) {
		int n = (in[2 + i / 2] >> (4 * (i & 1))) & 0xf;
		n = n >= 8 ? n - 16 : n;
		int predicted = (state->old * filter_pos[filter] + state->older * filter_neg[filter] + 32) >> 6;
		int decoded = clamp16((n << 12 >> shift) + predicted);
		out[i] = decoded;
		state->older = state->old;
		state->old = decoded;
	}
}

int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "usage: %s <input.qoa> <output.spu>\n", argv[0]);
		return 1;
	}
	qoa_desc qoa;
	short *pcm = qoa_read(argv[1], &qoa);
	if (!pcm || qoa.channels != 2) {
		fprintf(stderr, "%s: not a stereo QOA file\n", argv[1]);
		return 1;
	}
	unsigned int frames = qoa.samples;
	// Block 0 of each channel is the header or silence; samples start at block 1.
	unsigned int blocks = 1 + (frames + BLOCK_SAMPLES - 1) / BLOCK_SAMPLES;
	unsigned int chunks = (blocks + CHUNK_BLOCKS - 1) / CHUNK_BLOCKS + 1; // + the silent one
	size_t channel_bytes = (size_t)chunks * CHUNK_BLOCKS * BLOCK_BYTES;

	// Samples padded with silence to whole blocks.
	short *padded = calloc((size_t)(blocks - 1) * BLOCK_SAMPLES * 2, sizeof(short));
	memcpy(padded, pcm, (size_t)frames * 2 * sizeof(short));

	uint8_t *channel[2];
	adpcm_state_t enc[2] = {{0}}, dec[2] = {{0}};
	double signal = 0, noise = 0;
	for (int c = 0; c < 2; c++) {
		channel[c] = calloc(channel_bytes, 1);
		// Silent blocks (and the padding) decode to zero at shift 12.
		for (size_t b = 0; b < channel_bytes / BLOCK_BYTES; b++) {
			channel[c][b * BLOCK_BYTES] = 12;
		}
		for (unsigned int b = 1; b < blocks; b++) {
			const short *x = padded + (size_t)(b - 1) * BLOCK_SAMPLES * 2 + c;
			uint8_t *block = channel[c] + (size_t)b * BLOCK_BYTES;
			encode_block(x, 2, &enc[c], block);

			short decoded[BLOCK_SAMPLES];
			decode_block(block, &dec[c], decoded);
			for (int i = 0; i < BLOCK_SAMPLES; i++) {
				double s = x[i * 2], e = s - decoded[i];
				signal += s * s;
				noise += e * e;
			}
		}
	}

	// The header, in the left channel's first block: shift 12 and filter 0 in
	// byte 0, so its bytes decode to within +-8 of silence.
	uint32_t header[4] = {
		12,
		qoa.channels << 8,
		(uint32_t)lround(qoa.samplerate * 4096.0 / 48000.0), // SPU2 pitch: 0x1000 is 48 kHz
		0,
	};
	memcpy(channel[0], header, sizeof(header));

	FILE *f = fopen(argv[2], "wb");
	if (!f) {
		fprintf(stderr, "%s: can't write\n", argv[2]);
		return 1;
	}
	for (unsigned int k = 0; k < chunks; k++) {
		for (int c = 0; c < 2; c++) {
			fwrite(channel[c] + (size_t)k * CHUNK_BLOCKS * BLOCK_BYTES, BLOCK_BYTES, CHUNK_BLOCKS, f);
		}
	}
	fclose(f);

	printf("%s: %u samples at %u Hz, %u chunks, %.1f MB, SNR %.1f dB against the QOA\n",
		argv[2], frames, qoa.samplerate, chunks, chunks * 2.0 * CHUNK_BLOCKS * BLOCK_BYTES / 1e6,
		10 * log10(signal / (noise > 0 ? noise : 1)));
	return 0;
}
