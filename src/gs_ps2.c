#include <kernel.h>
#include <dma.h>
#include <gif_tags.h>
#include <gs_gp.h>

#include "gs_ps2.h"

// 64 KB of qwords. Each qword is one A+D register write (value, register).
#define PACKET_QWORDS 4096

static u64 packet[PACKET_QWORDS * 2] __attribute__((aligned(64)));
static int packet_len = 1; // qword 0 is the GIF tag

void gs_packet_init(void) {
	dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
	packet_len = 1;
}

void gs_flush(void) {
	int writes = packet_len - 1;
	if (writes == 0) {
		return;
	}
	packet[0] = GIF_SET_TAG(writes, 1, 0, 0, GIF_FLG_PACKED, 1);
	packet[1] = GIF_REG_AD;

	FlushCache(0);
	dma_channel_wait(DMA_CHANNEL_GIF, 0); // gsKit may still be sending its queue
	dma_channel_send_normal(DMA_CHANNEL_GIF, packet, packet_len, 0, 0);
	dma_channel_wait(DMA_CHANNEL_GIF, 0);
	packet_len = 1;
}

static inline void add(u64 reg, u64 value) {
	packet[packet_len * 2] = value;
	packet[packet_len * 2 + 1] = reg;
	packet_len++;
}

void gs_set_reg(u64 reg, u64 value) {
	if (packet_len + 1 > PACKET_QWORDS) {
		gs_flush();
	}
	add(reg, value);
}

#define PRIM_TME (1 << 4) // textured
#define PRIM_FST (1 << 8) // texture coordinates are UV, not STQ

void gs_draw(u64 prim, const gs_vertex_t *v, int n) {
	int textured = (prim & PRIM_TME) != 0;
	u64 uv_reg = (prim & PRIM_FST) ? GS_REG_UV : GS_REG_ST;
	int per_vertex = textured ? 3 : 2;
	if (packet_len + 1 + n * per_vertex > PACKET_QWORDS) {
		gs_flush();
	}
	add(GS_REG_PRIM, prim);
	for (int i = 0; i < n; i++) {
		add(GS_REG_RGBAQ, v[i].rgbaq);
		if (textured) {
			add(uv_reg, v[i].uv);
		}
		add(GS_REG_XYZ2, v[i].xyz);
	}
}

void gs_upload(u32 dbp, u32 dbw, u32 psm, int w, int h, const void *pixels, u32 bytes) {
	static u64 head[12] __attribute__((aligned(64)));
	u32 qwords = bytes / 16;

	gs_flush();

	head[0] = GIF_SET_TAG(4, 0, 0, 0, GIF_FLG_PACKED, 1);
	head[1] = GIF_REG_AD;
	head[2] = GS_SET_BITBLTBUF(0, 0, 0, dbp, dbw, psm);
	head[3] = GS_REG_BITBLTBUF;
	head[4] = GS_SET_TRXPOS(0, 0, 0, 0, 0);
	head[5] = GS_REG_TRXPOS;
	head[6] = GS_SET_TRXREG(w, h);
	head[7] = GS_REG_TRXREG;
	head[8] = GS_SET_TRXDIR(0); // EE to GS
	head[9] = GS_REG_TRXDIR;
	head[10] = GIF_SET_TAG(qwords, 1, 0, 0, GIF_FLG_IMAGE, 0);
	head[11] = 0;

	FlushCache(0);
	dma_channel_send_normal(DMA_CHANNEL_GIF, head, 6, 0, 0);
	dma_channel_wait(DMA_CHANNEL_GIF, 0);
	dma_channel_send_normal(DMA_CHANNEL_GIF, (void *)pixels, qwords, 0, 0);
	dma_channel_wait(DMA_CHANNEL_GIF, 0);
}
