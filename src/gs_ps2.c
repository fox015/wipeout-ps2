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

void gs_draw(u64 prim, const gs_vertex_t *v, int n) {
	if (packet_len + 1 + n * 2 > PACKET_QWORDS) {
		gs_flush();
	}
	add(GS_REG_PRIM, prim);
	for (int i = 0; i < n; i++) {
		add(GS_REG_RGBAQ, v[i].rgbaq);
		add(GS_REG_XYZ2, v[i].xyz);
	}
}
