#include <kernel.h>
#include <dma.h>
#include <gif_tags.h>
#include <gs_gp.h>

#include "gs_ps2.h"
#include "prof_ps2.h"

// 64 KB of qwords: GIF tags, each followed by its data. Register writes in a
// row share one A+D tag (a qword per write: value, then register). Each
// primitive is a REGLIST tag, whose data is the registers' 64-bit values
// packed two to a qword, so a textured vertex is 24 bytes, not A+D's 48.
#define PACKET_QWORDS 4096

static u64 packet[PACKET_QWORDS * 2] __attribute__((aligned(64)));
static int packet_len = 0;
static int last_tag = -1; // gets EOP when the packet is sent
static int ad_tag = -1;   // the A+D tag register writes are added to, if any

static void prim_tags_init(void);

void gs_packet_init(void) {
	dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
	packet_len = 0;
	last_tag = ad_tag = -1;
	prim_tags_init();
}

void gs_flush(void) {
	if (packet_len == 0) {
		return;
	}
	packet[last_tag * 2] |= GIF_SET_TAG(0, 1, 0, 0, 0, 0);

	FlushCache(0);
	uint32_t t0 = prof_cycles();
	dma_channel_wait(DMA_CHANNEL_GIF, 0); // gsKit may still be sending its queue
	dma_channel_send_normal(DMA_CHANNEL_GIF, packet, packet_len, 0, 0);
	dma_channel_wait(DMA_CHANNEL_GIF, 0);
	prof.dma_wait += prof_cycles() - t0;
	prof.qwords += packet_len;
	packet_len = 0;
	last_tag = ad_tag = -1;
}

static inline void add_tag(u64 tag, u64 regs) {
	last_tag = packet_len;
	packet[packet_len * 2] = tag;
	packet[packet_len * 2 + 1] = regs;
	packet_len++;
}

void gs_set_reg(u64 reg, u64 value) {
	if (packet_len + 2 > PACKET_QWORDS) {
		gs_flush();
	}
	if (ad_tag < 0) {
		ad_tag = packet_len;
		add_tag(GIF_SET_TAG(0, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
	}
	packet[ad_tag * 2]++; // NLOOP, the count of writes, is the tag's low bits
	packet[packet_len * 2] = value;
	packet[packet_len * 2 + 1] = reg;
	packet_len++;
}

#define PRIM_TME (1 << 4) // textured
#define PRIM_FST (1 << 8) // texture coordinates are UV, not STQ

// Room for count 64-bit values after a tag, zeroing the unused half of the
// last qword when count is odd.
static inline u64 *add_data(int count) {
	u64 *data = &packet[packet_len * 2];
	packet_len += (count + 1) / 2;
	if (count & 1) {
		data[count] = 0;
	}
	return data;
}

// A tag lists at most 16 registers: PRIM and up to 5 textured vertices (all
// the game's own polygons) fit in one tag. Those tags, and their register
// lists, are built once here, by the kind of primitive (untextured, ST, UV)
// and its vertex count.
#define PRIM_KINDS 3
#define PRIM_MAX_N 8
static const u64 kind_uv_reg[PRIM_KINDS] = {0, GIF_REG_ST, GIF_REG_UV};
static u64 prim_tag[PRIM_KINDS][PRIM_MAX_N], prim_regs[PRIM_KINDS][PRIM_MAX_N];

static inline int prim_kind(u64 prim) {
	return (prim & PRIM_TME) ? ((prim & PRIM_FST) ? 2 : 1) : 0;
}

static inline int per_vertex(int kind) {
	return kind ? 3 : 2;
}

static inline u64 vertex_regs(int kind) {
	return kind
		? GIF_REG_RGBAQ | kind_uv_reg[kind] << 4 | (u64)GIF_REG_XYZ2 << 8
		: GIF_REG_RGBAQ | (u64)GIF_REG_XYZ2 << 4;
}

static void prim_tags_init(void) {
	for (int kind = 0; kind < PRIM_KINDS; kind++) {
		for (int n = 1; n < PRIM_MAX_N; n++) {
			int count = 1 + n * per_vertex(kind);
			if (count > 16) {
				break;
			}
			u64 regs = GIF_REG_PRIM;
			for (int i = 0; i < n; i++) {
				regs |= vertex_regs(kind) << (4 * (1 + i * per_vertex(kind)));
			}
			prim_tag[kind][n] = GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_REGLIST, count);
			prim_regs[kind][n] = regs;
		}
	}
}

// Larger polygons, from the clipper, write PRIM in a tag of its own and loop
// over the vertices.
u64 *gs_prim(u64 prim, int n) {
	int kind = prim_kind(prim);
	int count = 1 + n * per_vertex(kind);
	int one_tag = count <= 16;
	int qwords = one_tag ? 1 + (count + 1) / 2 : 3 + count / 2;
	if (packet_len + qwords > PACKET_QWORDS) {
		gs_flush();
	}
	ad_tag = -1;

	if (one_tag) {
		add_tag(prim_tag[kind][n], prim_regs[kind][n]);
		u64 *data = add_data(count);
		data[0] = prim;
		return data + 1;
	}
	add_tag(GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_REGLIST, 1), GIF_REG_PRIM);
	add_data(1)[0] = prim;
	add_tag(GIF_SET_TAG(n, 0, 0, 0, GIF_FLG_REGLIST, per_vertex(kind)), vertex_regs(kind));
	return add_data(count - 1);
}

void gs_draw(u64 prim, const gs_vertex_t *v, int n) {
	int textured = (prim & PRIM_TME) != 0;
	u64 *d = gs_prim(prim, n);
	for (int i = 0; i < n; i++) {
		*d++ = v[i].rgbaq;
		if (textured) {
			*d++ = v[i].uv;
		}
		*d++ = v[i].xyz;
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
