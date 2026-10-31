// Saves on the memory card in slot 1, through libmc.
//
// The game stores one file, its settings and records, whenever it changes
// scene with something new to save (game_set_scene()). It lives in the save
// folder SAVE_DIR. Each libmc call is asynchronous on the IOP; mc_wait()
// waits for it, so a save holds up the frame it happens in.
//
// The folder also gets icon.sys and an icon, so the PS2's browser lists the
// save. The icon is the Feisar ship, converted when it's written from the
// model and textures the game loaded from the player's own data. Nothing of
// the game's is in this source.

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <libmc.h>
#include <sjis.h>

#include "mem.h"
#include "platform.h"
#include "render.h"
#include "render_ps2.h"
#include "wipeout/object.h"
#include "wipeout/game.h"

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

static int write_file(const char *name, const void *bytes, int len) {
	char path[64];
	snprintf(path, sizeof(path), "%s/%s", SAVE_DIR, name);
	mcOpen(PORT, SLOT, path, sceMcFileAttrWriteable | sceMcFileCreateFile);
	int fd = mc_wait();
	if (fd < 0) {
		printf("memcard: could not open %s (%d)\n", path, fd);
		return 0;
	}
	// libmc transfers to and from 64-byte aligned buffers.
	uint8_t *buf = memalign(64, len);
	memcpy(buf, bytes, len);
	mcWrite(fd, buf, len);
	int written = mc_wait();
	free(buf);
	mcClose(fd);
	mc_wait();
	if (written != len) {
		printf("memcard: wrote %d of %d bytes to %s\n", written, len, path);
		return 0;
	}
	return 1;
}

// ---------------------------------------------------------------------------
// The browser icon
//
// A PS2 icon file is a header, the vertices (position, normal, UV and colour
// each, positions and normals 4096 = 1.0, y down), an animation block and a
// 128x128 16-bit texture.

#define ICON_FILE "icon.ico"
#define ICON_TEX_SIZE 128

typedef struct {
	uint32_t id;          // 0x010000
	uint32_t shapes;      // animation shapes
	uint32_t texture;     // 7: uncompressed texture
	float reserved;       // 1.0
	uint32_t vertices;    // a multiple of 3
} icon_header_t;

typedef struct {
	int16_t x, y, z, pad;
	int16_t nx, ny, nz, npad;
	int16_t u, v;
	uint8_t r, g, b, a;
} icon_vertex_t;

typedef struct {
	uint32_t id;          // 1
	uint32_t frame_length;
	float speed;
	uint32_t offset;
	uint32_t frames;
	// one frame
	uint32_t shape;
	uint32_t keys;
	uint32_t unknown[2];
	// one key
	float time, value;
} icon_anim_t;

// The atlas: every texture the model uses, packed into the icon's one
// 128x128 texture, plus a white patch for untextured faces.
#define ICON_MAX_TEXTURES 32
#define ICON_WHITE 4 // texels square

typedef struct {
	uint16_t texture;
	int x, y; // in the atlas
} icon_slot_t;

typedef struct {
	icon_slot_t slots[ICON_MAX_TEXTURES];
	int len;
	float scale; // atlas texels per source texel
} icon_atlas_t;

static int atlas_slot(icon_atlas_t *atlas, uint16_t texture) {
	for (int i = 0; i < atlas->len; i++) {
		if (atlas->slots[i].texture == texture) {
			return i;
		}
	}
	if (atlas->len == ICON_MAX_TEXTURES) {
		return -1;
	}
	atlas->slots[atlas->len].texture = texture;
	return atlas->len++;
}

// Shelf packing, tallest first, at atlas->scale. The white patch is at 0,0.
static int atlas_pack(icon_atlas_t *atlas) {
	int order[ICON_MAX_TEXTURES];
	for (int i = 0; i < atlas->len; i++) {
		order[i] = i;
	}
	for (int i = 1; i < atlas->len; i++) {
		for (int j = i; j > 0 && render_texture_size(atlas->slots[order[j]].texture).y >
				render_texture_size(atlas->slots[order[j - 1]].texture).y; j--) {
			int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
		}
	}
	int x = ICON_WHITE, y = 0, shelf = ICON_WHITE;
	for (int i = 0; i < atlas->len; i++) {
		icon_slot_t *slot = &atlas->slots[order[i]];
		vec2i_t size = render_texture_size(slot->texture);
		int w = ceilf(size.x * atlas->scale), h = ceilf(size.y * atlas->scale);
		if (x + w > ICON_TEX_SIZE) {
			x = 0;
			y += shelf;
			shelf = 0;
		}
		if (w > ICON_TEX_SIZE || y + h > ICON_TEX_SIZE) {
			return 0;
		}
		slot->x = x;
		slot->y = y;
		x += w;
		shelf = max(shelf, h);
	}
	return 1;
}

// One triangle of the model, as icon vertices.
static uint8_t *icon_tri(uint8_t *p, Object *model, const int16_t coords[3], const uint8_t uv[3][2],
		icon_slot_t *slot, float scale, vec3_t centre, float size) {
	// The browser's front faces wind the other way from the game's, so each
	// triangle is reversed (corner 0, 2, 1); its normal follows.
	static const int order[3] = {0, 2, 1};
	vec3_t v[3];
	for (int i = 0; i < 3; i++) {
		vec4_t *mv = &model->vertices[coords[order[i]]];
		// Side on: the ship's length (z) along the icon's x.
		v[i] = vec3((mv->z - centre.z) * size, (mv->y - centre.y) * size, -(mv->x - centre.x) * size);
	}
	vec3_t n = vec3_cross(vec3_sub(v[1], v[0]), vec3_sub(v[2], v[0]));
	float len = vec3_len(n);
	if (len > 0) {
		n = vec3_mulf(n, 1.0f / len);
	}
	for (int i = 0; i < 3; i++) {
		float u = ICON_WHITE / 2, t = ICON_WHITE / 2; // untextured: the white patch
		if (slot) {
			u = slot->x + (uv[order[i]][0] + 0.5f) * scale;
			t = slot->y + (uv[order[i]][1] + 0.5f) * scale;
		}
		icon_vertex_t iv = {
			.x = v[i].x * 4096, .y = v[i].y * 4096, .z = v[i].z * 4096,
			.nx = n.x * 4096, .ny = n.y * 4096, .nz = n.z * 4096,
			.u = u * (4096 / ICON_TEX_SIZE), .v = t * (4096 / ICON_TEX_SIZE),
			.r = 0x80, .g = 0x80, .b = 0x80, .a = 0x80,
		};
		memcpy(p, &iv, sizeof(iv));
		p += sizeof(iv);
	}
	return p;
}

static Object *feisar_model(void) {
	for (int i = 0; i < NUM_PILOTS; i++) {
		if (def.pilots[i].team == TEAM_FEISAR) {
			return g.ships[i].model;
		}
	}
	return NULL;
}

static int write_icon_file(void) {
	Object *model = feisar_model();
	if (!model) {
		return 0;
	}

	// Count the triangles (quads are two), find the textures and the bounds.
	icon_atlas_t atlas = {.len = 0, .scale = 1.0f};
	int tris = 0;
	vec3_t lo = vec3(1e9f, 1e9f, 1e9f), hi = vec3(-1e9f, -1e9f, -1e9f);
	Prm poly = {.primitive = model->primitives};
	for (int i = 0; i < model->primitives_len; i++) {
		switch (poly.primitive->type) {
		case PRM_TYPE_F3:  tris += 1; poly.f3 += 1; break;
		case PRM_TYPE_G3:  tris += 1; poly.g3 += 1; break;
		case PRM_TYPE_F4:  tris += 2; poly.f4 += 1; break;
		case PRM_TYPE_G4:  tris += 2; poly.g4 += 1; break;
		case PRM_TYPE_FT3: tris += 1; atlas_slot(&atlas, poly.ft3->texture); poly.ft3 += 1; break;
		case PRM_TYPE_GT3: tris += 1; atlas_slot(&atlas, poly.gt3->texture); poly.gt3 += 1; break;
		case PRM_TYPE_FT4: tris += 2; atlas_slot(&atlas, poly.ft4->texture); poly.ft4 += 1; break;
		case PRM_TYPE_GT4: tris += 2; atlas_slot(&atlas, poly.gt4->texture); poly.gt4 += 1; break;
		case PRM_TYPE_TSPR:
		case PRM_TYPE_BSPR: poly.spr += 1; break;
		default: i = model->primitives_len; break; // unknown size: stop
		}
	}
	for (int i = 0; i < model->vertices_len; i++) {
		vec4_t *v = &model->vertices[i];
		lo = vec3(min(lo.x, v->x), min(lo.y, v->y), min(lo.z, v->z));
		hi = vec3(max(hi.x, v->x), max(hi.y, v->y), max(hi.z, v->z));
	}
	while (!atlas_pack(&atlas)) {
		atlas.scale *= 0.5f;
	}

	// About 5 icon units long, centred, resting just above the floor (y = 0,
	// y down in both the model and the icon).
	float extent = max(max(hi.x - lo.x, hi.y - lo.y), hi.z - lo.z);
	float size = 5.0f / extent;
	vec3_t centre = vec3((lo.x + hi.x) / 2, hi.y + 0.2f / size, (lo.z + hi.z) / 2);

	int bytes = sizeof(icon_header_t) + tris * 3 * sizeof(icon_vertex_t) +
		sizeof(icon_anim_t) + ICON_TEX_SIZE * ICON_TEX_SIZE * 2;
	uint8_t *file = malloc(bytes);
	uint8_t *p = file;

	icon_header_t header = {.id = 0x010000, .shapes = 1, .texture = 7, .reserved = 1.0f, .vertices = tris * 3};
	memcpy(p, &header, sizeof(header));
	p += sizeof(header);

	poly.primitive = model->primitives;
	for (int i = 0; i < model->primitives_len; i++) {
		int16_t c[4];
		uint8_t uv[4][2] = {{0}};
		icon_slot_t *slot = NULL;
		int quad = 0;
		switch (poly.primitive->type) {
		case PRM_TYPE_F3: memcpy(c, poly.f3->coords, 6); poly.f3 += 1; break;
		case PRM_TYPE_G3: memcpy(c, poly.g3->coords, 6); poly.g3 += 1; break;
		case PRM_TYPE_F4: memcpy(c, poly.f4->coords, 8); quad = 1; poly.f4 += 1; break;
		case PRM_TYPE_G4: memcpy(c, poly.g4->coords, 8); quad = 1; poly.g4 += 1; break;
		case PRM_TYPE_FT3: case PRM_TYPE_GT3: {
			FT3 *f = poly.ft3; // FT3 and GT3 share the leading fields
			memcpy(c, f->coords, 6);
			slot = &atlas.slots[atlas_slot(&atlas, f->texture)];
			uint8_t t[3][2] = {{f->u0, f->v0}, {f->u1, f->v1}, {f->u2, f->v2}};
			memcpy(uv, t, sizeof(t));
			if (poly.primitive->type == PRM_TYPE_FT3) poly.ft3 += 1; else poly.gt3 += 1;
			break;
		}
		case PRM_TYPE_FT4: case PRM_TYPE_GT4: {
			FT4 *f = poly.ft4; // FT4 and GT4 share the leading fields
			memcpy(c, f->coords, 8);
			slot = &atlas.slots[atlas_slot(&atlas, f->texture)];
			uint8_t t[4][2] = {{f->u0, f->v0}, {f->u1, f->v1}, {f->u2, f->v2}, {f->u3, f->v3}};
			memcpy(uv, t, sizeof(t));
			quad = 1;
			if (poly.primitive->type == PRM_TYPE_FT4) poly.ft4 += 1; else poly.gt4 += 1;
			break;
		}
		case PRM_TYPE_TSPR:
		case PRM_TYPE_BSPR: poly.spr += 1; continue;
		default: i = model->primitives_len; continue;
		}
		p = icon_tri(p, model, c, (const uint8_t (*)[2])uv, slot, atlas.scale, centre, size);
		if (quad) {
			// The PS1's quads are strips: 0 1 2, then 2 1 3.
			int16_t c2[3] = {c[2], c[1], c[3]};
			uint8_t uv2[3][2] = {{uv[2][0], uv[2][1]}, {uv[1][0], uv[1][1]}, {uv[3][0], uv[3][1]}};
			p = icon_tri(p, model, c2, (const uint8_t (*)[2])uv2, slot, atlas.scale, centre, size);
		}
	}

	icon_anim_t anim = {
		.id = 1, .frame_length = 1, .speed = 1.0f, .offset = 0, .frames = 1,
		.shape = 0, .keys = 1, .unknown = {1, 0}, .time = 1.0f, .value = 1.0f,
	};
	memcpy(p, &anim, sizeof(anim));
	p += sizeof(anim);

	// The atlas texture, sampled from the game's textures.
	uint16_t *tex = (uint16_t *)p;
	memset(tex, 0, ICON_TEX_SIZE * ICON_TEX_SIZE * 2);
	for (int y = 0; y < ICON_WHITE; y++) {
		for (int x = 0; x < ICON_WHITE; x++) {
			tex[y * ICON_TEX_SIZE + x] = 0xffff;
		}
	}
	for (int i = 0; i < atlas.len; i++) {
		icon_slot_t *slot = &atlas.slots[i];
		vec2i_t sz = render_texture_size(slot->texture);
		int w = ceilf(sz.x * atlas.scale), h = ceilf(sz.y * atlas.scale);
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				uint16_t c = render_ps2_texel(slot->texture, x / atlas.scale, y / atlas.scale);
				tex[(slot->y + y) * ICON_TEX_SIZE + slot->x + x] = c | 0x8000;
			}
		}
	}

	printf("memcard: icon from the Feisar: %d triangles, %d textures at scale %.2f\n",
		tris, atlas.len, (double)atlas.scale);
	int ok = write_file(ICON_FILE, file, bytes);
	free(file);
	return ok;
}

static int write_icon_sys(void) {
	static const iconIVECTOR bg[4] = {
		{0x00, 0x00, 0x30, 0}, {0x00, 0x00, 0x30, 0},
		{0x00, 0x10, 0x50, 0}, {0x00, 0x10, 0x50, 0},
	};
	static const iconFVECTOR light_dir[3] = {
		{ 0.5f,  0.5f, 0.5f, 0.0f},
		{ 0.0f, -0.4f, -0.1f, 0.0f},
		{-0.5f, -0.5f, 0.5f, 0.0f},
	};
	static const iconFVECTOR light_col[3] = {
		{0.6f, 0.6f, 0.6f, 0.0f},
		{0.4f, 0.4f, 0.4f, 0.0f},
		{0.3f, 0.3f, 0.3f, 0.0f},
	};
	static const iconFVECTOR ambient = {0.7f, 0.7f, 0.7f, 0.0f};

	mcIcon icon;
	memset(&icon, 0, sizeof(icon));
	memcpy(icon.head, "PS2D", 4);
	icon.type = MCICON_TYPE_SAVED_DATA;
	// Two lines; the second starts after the 7 two-byte characters of the first.
	// The browser shows up to 16 characters a line.
	strcpy_sjis((short *)icon.title, "wipEoutSettings & Times");
	icon.nlOffset = 7 * 2;
	icon.trans = 0x60;
	memcpy(icon.bgCol, bg, sizeof(bg));
	memcpy(icon.lightDir, light_dir, sizeof(light_dir));
	memcpy(icon.lightCol, light_col, sizeof(light_col));
	memcpy(icon.lightAmbient, ambient, sizeof(ambient));
	strcpy((char *)icon.view, ICON_FILE);
	strcpy((char *)icon.copy, ICON_FILE);
	strcpy((char *)icon.del, ICON_FILE);
	return write_file("icon.sys", &icon, sizeof(icon));
}

// Writes the icon files unless the folder already has them.
static void ensure_icon(void) {
	mcOpen(PORT, SLOT, SAVE_DIR "/icon.sys", sceMcFileAttrReadable);
	int fd = mc_wait();
	if (fd >= 0) {
		mcClose(fd);
		mc_wait();
		return;
	}
	if (write_icon_file() && write_icon_sys()) {
		printf("memcard: wrote the browser icon\n");
	}
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

	ensure_icon();
	int ok = write_file(name, bytes, len);
	printf("memcard: save %s/%s (%d bytes)%s\n", SAVE_DIR, name, (int)len, ok ? "" : " failed");
	return ok ? len : 0;
}
