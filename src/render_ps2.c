// PS2 renderer. gsKit sets up the display, clears and flips; everything else
// is drawn through our own GIF packets (gs_ps2.c), which control the GS
// registers directly.
//
// 3D follows the Dreamcast renderer: the current matrix (mtx.h) holds
// projection * view * model, vertices are transformed without the perspective
// divide, polygons outside the frustum are dropped, those crossing the near
// plane or the GS guard band are clipped, and depth is 1/w.
//
// Textures are kept in RAM and uploaded to a VRAM cache as they're drawn
// (see "Textures" below).

#include <malloc.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <gs_gp.h>

#include "render.h"
#include "mtx.h"
#include "system.h"
#include "utils.h"
#include "gs_ps2.h"
#include "wipeout/game.h"
#include "todo_ps2.h"

#define TEXTURES_MAX 1024

#define NEAR_PLANE 16.0f

// 1/w is at most 1/NEAR_PLANE, which maps to the top of the 16-bit Z range.
#define Z_SCALE (NEAR_PLANE * 65535.0f)

// The GS draws within +-2048 pixels of its drawing offset. Clip anything
// further than this many half-screens from the centre.
#define GUARD_BAND 5.0f

// Polygons whose screen-space winding has this sign are back faces.
#define BACKFACE_SIGN -1.0f

global_render_state_t render_state;
render_vertex_t vs[5];
uint16_t RENDER_NO_TEXTURE;
const uint16_t HUD_NO_TEXTURE = 65535;

// hud.c projects the target reticle with these
mat4_t view_mat = mat4_identity();
mat4_t mvp_mat = mat4_identity();
mat4_t vp_mat = mat4_identity();

static mat4_t projection_mat = mat4_identity();
static mat4_t sprite_mat = mat4_identity();

static GSGLOBAL *gs;

#if defined(VIDEO_480p)
#define defined_video_mode GS_MODE_DTV_480P
#elif defined(VIDEO_vga)
#define defined_video_mode GS_MODE_VGA_640_60
#endif

// The game lays out 640x480 (or 640x360 for 16:9); the GS output may have
// fewer lines. scale_y maps 480 lines to the output.
static vec2i_t screen_size = {640, 480};
static float scale_y = 1.0f;

static float shake_x = 320.0f;
static float shake_y = 240.0f;
static float shake_count = 0.0f;

// GS state as last sent, so register writes are only queued on a change.
static int depth_test = 1;
static int depth_write = 1;
static int cull_backface = 1;
static u64 sent_test = ~0ull;
static u64 sent_zbuf = ~0ull;

static void textures_vram_init(void);
static int is_textured(uint16_t texture_index);
static void texture_bind(uint16_t texture_index);
static void textures_frame_start(void);
static void textures_frame_end(void);

void render_init(void) {
	gs = gsKit_init_global();
	gs->PSM = GS_PSM_CT32;
	gs->PSMZ = GS_PSMZ_16S;
	gs->ZBuffering = GS_SETTING_ON;
	gs->DoubleBuffering = GS_SETTING_ON;
	gs->PrimAlphaEnable = GS_SETTING_ON;

	// 480i is gsKit's default (NTSC, interlaced). The others are progressive,
	// so the full 480 lines are drawn every frame.
#if defined(VIDEO_480p) || defined(VIDEO_vga)
	gs->Mode = defined_video_mode;
	gs->Interlace = GS_NONINTERLACED;
	gs->Field = GS_FRAME;
	gs->Width = 640;
	gs->Height = 480;
#elif !defined(VIDEO_480i)
#error "VIDEO must be 480i, 480p or vga"
#endif

	dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
	            D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
	dmaKit_chan_init(DMA_CHANNEL_GIF);

	gsKit_init_screen(gs);

	// gsKit's clear uses its own primitive blend; set it to normal blending,
	// (Cs - Cd) * As + Cd. Its default, GS_BLEND_BACK2FRONT, keeps the
	// destination for opaque pixels, so nothing would show.
	gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

	gs_packet_init();

	scale_y = (float)gs->Height / 480.0f;
	printf("GS: %dx%d, interlace %d, field %d, offset %d,%d, zbuf 0x%x\n",
		gs->Width, gs->Height, gs->Interlace, gs->Field,
		gs->OffsetX, gs->OffsetY, (unsigned)gs->ZBuffer);

	textures_vram_init();
	render_set_screen_size(screen_size);
	RENDER_NO_TEXTURE = render_texture_create(2, 2, NULL);
}

void render_cleanup(void) {}

// ---------------------------------------------------------------------------
// Colour

static rgba_t rgba_from_argb(uint32_t argb) {
	return rgba((argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff, argb >> 24);
}

static uint32_t float_bits(float f) {
	union { float f; uint32_t u; } bits = {f};
	return bits.u;
}

// GS alpha 0x80 is fully opaque. The GS modulates a texture as
// texture * colour / 128, and the game's colours (made for the Dreamcast) use
// 255 for 1.0, so colours on textured polygons are halved too. q is 1/w, for
// perspective-correct texturing (ST is divided by it).
static u64 rgbaq_from(rgba_t c, int textured, float q) {
	if (textured) {
		c.r = (c.r + 1) >> 1;
		c.g = (c.g + 1) >> 1;
		c.b = (c.b + 1) >> 1;
	}
	return GS_SET_RGBAQ(c.r, c.g, c.b, (c.a + 1) >> 1, float_bits(q));
}

// ---------------------------------------------------------------------------
// GS state

// Screen coordinates in the game's 640x480 space to GS XYZ2.
static u64 xyz_from(float x, float y, uint32_t z) {
	int gx = (int)(x * 16.0f) + gs->OffsetX;
	int gy = (int)(y * scale_y * 16.0f) + gs->OffsetY;
	return GS_SET_XYZ(gx, gy, z);
}

// Don't switch the depth test off (ZTE=0): Sony's restrictions doc says it can
// misbehave. "Off" is ZTE=1 with ZTST=ALWAYS instead.
// The alpha test drops pixels with alpha 0 (transparent texels) without
// writing colour or Z.
static void sync_depth_state(void) {
	u64 test = GS_SET_TEST(1, 6, 0, 0, 0, 0, 1, depth_test ? 2 : 1); // alpha > 0; GEQUAL : ALWAYS
	u64 zbuf = GS_SET_ZBUF(gs->ZBuffer / 8192, gs->PSMZ, depth_write ? 0 : 1);
	if (test != sent_test) {
		gs_set_reg(GS_REG_TEST, test);
		sent_test = test;
	}
	if (zbuf != sent_zbuf) {
		gs_set_reg(GS_REG_ZBUF, zbuf);
		sent_zbuf = zbuf;
	}
}

// ---------------------------------------------------------------------------
// Frames

void render_frame_prepare(void) {
	// gsKit clears colour and Z right away, before any of our packets.
	gsKit_clear(gs, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x80, 0x00));
	gsKit_queue_exec(gs);
	gsKit_queue_reset(gs->Os_Queue);

	gs_set_reg(GS_REG_ALPHA, GS_SET_ALPHA(0, 1, 0, 1, 0));
	gs_set_reg(GS_REG_COLCLAMP, GS_SET_COLCLAMP(1));
	sent_test = sent_zbuf = ~0ull;
	textures_frame_start();
	render_set_depth_write(true);
	render_set_depth_test(true);
}

// Waits for vsync, so the main loop runs at the display rate.
void render_frame_end(void) {
	// Leave Z writes and the Z test as gsKit expects for its next clear.
	depth_write = 1;
	depth_test = 0;
	sync_depth_state();
	gs_flush();
	textures_frame_end();
	gsKit_sync_flip(gs);
}

// The GS draws in submission order, so there is no separate translucent pass.
void render_begin_translucent(void) {}

// ---------------------------------------------------------------------------
// Projection and view

// 16:9 is anamorphic, as on the Dreamcast: the game lays out 640x360 and the
// projection is 16:9, but the output still fills every line, so a 16:9 TV
// stretches it back to shape. Menu and HUD code check for the 360 height.
vec2i_t render_res_to_size(render_resolution_t res) {
	return res == RENDER_RES_43 ? vec2i(640, 480) : vec2i(640, 360);
}

void render_set_screen_size(vec2i_t size) {
	screen_size = size;

	float aspect = (float)size.x / (float)size.y;
	float fov = (73.75f / 180.0f) * F_PI;
	float f = 1.0f / tanf(fov * 0.5f);
	float nf = 1.0f / (NEAR_PLANE - 96000.0f);

	projection_mat = mat4(
		f / aspect, 0, 0, 0,
		0, f, 0, 0,
		0, 0, (96000.0f + NEAR_PLANE) * nf, -1,
		0, 0, 2 * 96000.0f * NEAR_PLANE * nf, 0);
	vp_mat = projection_mat;
}

void render_reset_proj(float farval) {
	float nf = 1.0f / (NEAR_PLANE - farval);
	float f1 = (farval + NEAR_PLANE) * nf;
	float f2 = 2 * farval * NEAR_PLANE * nf;

	projection_mat.m[10] = f1;
	projection_mat.m[14] = f2;
	vp_mat.m[10] = f1;
	vp_mat.m[14] = f2;
}

void render_set_resolution(render_resolution_t res) {
	render_set_screen_size(render_res_to_size(res));
}

void render_set_post_effect(render_post_effect_t post) {}

vec2i_t render_size(void) {
	return screen_size;
}

void render_set_view(vec3_t pos, vec3_t angles) {
	render_set_depth_write(true);
	render_set_depth_test(true);

	view_mat = mat4_identity();
	mat4_set_translation(&view_mat, vec3(0, 0, 0));
	mat4_set_roll_pitch_yaw(&view_mat, vec3(angles.x, -angles.y + F_PI, angles.z + F_PI));
	mat4_translate(&view_mat, vec3_inv(pos));
	mat4_set_yaw_pitch_roll(&sprite_mat, vec3(-angles.x, angles.y - F_PI, 0));
}

void render_set_view_2d(void) {
	render_set_depth_test(false);
	render_set_depth_write(false);

	float near = -1;
	float far = 1;
	float left = 0;
	float right = screen_size.x;
	float bottom = screen_size.y;
	float top = 0;
	float lr = 1 / (left - right);
	float bt = 1 / (bottom - top);
	float nf = 1 / (near - far);

	mvp_mat = mat4(
		-2 * lr, 0, 0, 0,
		0, -2 * bt, 0, 0,
		0, 0, 2 * nf, 0,
		(left + right) * lr, (top + bottom) * bt, (far + near) * nf, 1);

	mtx_load(&mvp_mat);
}

void render_set_model_mat(mat4_t *m) {
	mat4_t vpv;
	mtx_load_mul(&vp_mat, &view_mat);
	vpv = mtx_current;
	mtx_load_mul(&vpv, m);
}

void render_set_model_ident(void) {
	mtx_load_mul(&vp_mat, &view_mat);
}

void render_set_depth_write(bool enabled) { depth_write = enabled; }
void render_set_depth_test(bool enabled) { depth_test = enabled; }
void render_set_depth_offset(float offset) {}
void render_set_screen_position(vec2_t pos) {}
void render_set_blend_mode(render_blend_mode_t mode) { render_state.blend_mode = mode; }
void render_set_cull_backface(bool enabled) { cull_backface = enabled; }

// ---------------------------------------------------------------------------
// Screen shake from weapon hits (as on the Dreamcast)

void SetShake(float duration) {
	shake_count = duration / 30.0f;
}

void ShakeScreen(void) {
	if (shake_count > 0.0f) {
		float s = 0.5f * shake_count * 45.0f;
		shake_x = 320.0f + rand_float(-s, s);
		shake_y = 240.0f + rand_float(-s, s);
		shake_count -= system_tick();
	} else {
		shake_x = 320.0f;
		shake_y = 240.0f;
		shake_count = 0.0f;
	}
}

// ---------------------------------------------------------------------------
// Clipping and drawing

// A vertex in clip space.
typedef struct {
	float x, y, z, w;
	float u, v;
	rgba_t color;
} clip_vertex_t;

// Signed distance from a clip plane; >= 0 is inside.
typedef float (*plane_fn)(const clip_vertex_t *v);
static float plane_near(const clip_vertex_t *v)   { return v->z + v->w; }
static float plane_left(const clip_vertex_t *v)   { return GUARD_BAND * v->w + v->x; }
static float plane_right(const clip_vertex_t *v)  { return GUARD_BAND * v->w - v->x; }
static float plane_top(const clip_vertex_t *v)    { return GUARD_BAND * v->w - v->y; }
static float plane_bottom(const clip_vertex_t *v) { return GUARD_BAND * v->w + v->y; }

static const plane_fn clip_planes[] = {plane_near, plane_left, plane_right, plane_top, plane_bottom};

#define CLIP_MAX 16

// Sutherland-Hodgman against one plane. Returns the new vertex count.
static int clip_poly(const clip_vertex_t *in, int n, clip_vertex_t *out, plane_fn plane) {
	int out_n = 0;
	for (int i = 0; i < n; i++) {
		const clip_vertex_t *a = &in[i];
		const clip_vertex_t *b = &in[(i + 1) % n];
		float da = plane(a);
		float db = plane(b);
		if (da >= 0) {
			out[out_n++] = *a;
		}
		if ((da >= 0) != (db >= 0) && out_n < CLIP_MAX) {
			float t = da / (da - db);
			clip_vertex_t *c = &out[out_n++];
			c->x = a->x + (b->x - a->x) * t;
			c->y = a->y + (b->y - a->y) * t;
			c->z = a->z + (b->z - a->z) * t;
			c->w = a->w + (b->w - a->w) * t;
			c->u = a->u + (b->u - a->u) * t;
			c->v = a->v + (b->v - a->v) * t;
			c->color = a->color; // flat across the cut, as on the Dreamcast
		}
	}
	return out_n;
}

// Outcodes for the view frustum: a polygon with every vertex outside the
// same plane can't be seen.
static int outcode(const clip_vertex_t *v) {
	int code = 0;
	if (v->x < -v->w) code |= 1;
	if (v->x >  v->w) code |= 2;
	if (v->y < -v->w) code |= 4;
	if (v->y >  v->w) code |= 8;
	if (v->z < -v->w) code |= 16;
	if (v->z >  v->w) code |= 32;
	return code;
}

// Clip, project and draw a convex polygon given in clip space.
static void draw_clip_poly(clip_vertex_t *poly, int n, uint16_t texture_index) {
	int all_out = ~0;
	for (int i = 0; i < n; i++) {
		all_out &= outcode(&poly[i]);
	}
	if (all_out) {
		return;
	}

	clip_vertex_t buf_a[CLIP_MAX], buf_b[CLIP_MAX];
	clip_vertex_t *src = poly, *dst = buf_a;
	for (int p = 0; p < (int)(sizeof(clip_planes) / sizeof(clip_planes[0])); p++) {
		n = clip_poly(src, n, dst, clip_planes[p]);
		if (n < 3) {
			return;
		}
		src = dst;
		dst = (dst == buf_a) ? buf_b : buf_a;
	}

	int textured = is_textured(texture_index);
	gs_vertex_t gv[CLIP_MAX];
	float sx[CLIP_MAX], sy[CLIP_MAX];
	for (int i = 0; i < n; i++) {
		float rw = 1.0f / src[i].w;
		sx[i] = shake_x + 320.0f * src[i].x * rw;
		sy[i] = shake_y - 240.0f * src[i].y * rw;
		float z = rw * Z_SCALE;
		uint32_t iz = z > 65535.0f ? 65535 : (uint32_t)z;
		gv[i].rgbaq = rgbaq_from(src[i].color, textured, rw);
		gv[i].uv = GS_SET_ST(float_bits(src[i].u * rw), float_bits(src[i].v * rw));
		gv[i].xyz = xyz_from(sx[i], sy[i], iz);
	}

	if (cull_backface) {
		float area = 0;
		for (int i = 0; i < n; i++) {
			int j = (i + 1) % n;
			area += sx[i] * sy[j] - sx[j] * sy[i];
		}
		if (area * BACKFACE_SIGN > 0) {
			return;
		}
	}

	sync_depth_state();
	if (textured) {
		texture_bind(texture_index);
	}

	// Fan out from vertex 0.
	u64 prim = GS_SET_PRIM(GS_PRIM_TRIANGLE, 1, textured, 0, 1, 0, 0, 0, 0);
	for (int i = 1; i + 1 < n; i++) {
		gs_vertex_t tri[3] = {gv[0], gv[i], gv[i + 1]};
		gs_draw(prim, tri, 3);
	}
}

// vs[] in the order of a triangle strip (0,1,2 and 1,2,3) to a polygon, in
// clip space, with each position already transformed and its w given.
static void draw_vs(const int *order, int n, const float *w, uint16_t texture_index) {
	clip_vertex_t poly[4];
	for (int i = 0; i < n; i++) {
		const render_vertex_t *v = &vs[order[i]];
		poly[i].x = v->x;
		poly[i].y = v->y;
		poly[i].z = v->z;
		poly[i].w = w[order[i]];
		poly[i].u = v->u;
		poly[i].v = v->v;
		poly[i].color = rgba_from_argb(v->argb);
	}
	draw_clip_poly(poly, n, texture_index);
}

static const int quad_order[4] = {0, 1, 3, 2};
static const int tri_order[3] = {0, 1, 2};

// Transform vs[0..n) by the current matrix into clip space, keeping w.
static void transform_vs(int n, float *w) {
	const float *m = mtx_current.m;
	for (int i = 0; i < n; i++) {
		float x = vs[i].x, y = vs[i].y, z = vs[i].z;
		vs[i].x = m[0] * x + m[4] * y + m[8]  * z + m[12];
		vs[i].y = m[1] * x + m[5] * y + m[9]  * z + m[13];
		vs[i].z = m[2] * x + m[6] * y + m[10] * z + m[14];
		w[i]    = m[3] * x + m[7] * y + m[11] * z + m[15];
	}
}

void render_quad(uint16_t texture_index) {
	float w[4];
	transform_vs(4, w);
	draw_vs(quad_order, 4, w, texture_index);
}

void render_tri(uint16_t texture_index) {
	float w[3];
	transform_vs(3, w);
	draw_vs(tri_order, 3, w, texture_index);
}

void render_quad_noxform(uint16_t texture_index, float *w) {
	draw_vs(quad_order, 4, w, texture_index);
}

void render_tri_noxform(uint16_t texture_index, float *w) {
	draw_vs(tri_order, 3, w, texture_index);
}

// The caller has checked every vertex is in front of the near plane; the
// clipper finds nothing to cut, so these share the clipping path.
void render_quad_noxform_noclip(uint16_t texture_index, float *w) {
	draw_vs(quad_order, 4, w, texture_index);
}

void render_tri_noxform_noclip(uint16_t texture_index, float *w) {
	draw_vs(tri_order, 3, w, texture_index);
}

void render_hud_quad(uint16_t texture_index) {
	render_quad(texture_index);
}

// A camera-facing quad at pos, built with the sprite matrix from render_set_view().
void render_push_sprite(vec3_t pos, vec2i_t size, uint32_t color, uint16_t texture_index) {
	mat4_t saved = mtx_current;
	mtx_load(&sprite_mat);
	vec3_t t1 = vec3_transform(vec3( size.x * 0.5f, -size.y * 0.5f, 0));
	vec3_t t2 = vec3_transform(vec3(-size.x * 0.5f, -size.y * 0.5f, 0));
	vec3_t t3 = vec3_transform(vec3( size.x * 0.5f,  size.y * 0.5f, 0));
	vec3_t t4 = vec3_transform(vec3(-size.x * 0.5f,  size.y * 0.5f, 0));
	mtx_load(&saved);

	vec3_t p[4] = {vec3_add(pos, t1), vec3_add(pos, t2), vec3_add(pos, t3), vec3_add(pos, t4)};
	for (int i = 0; i < 4; i++) {
		vs[i].x = p[i].x;
		vs[i].y = p[i].y;
		vs[i].z = p[i].z;
		vs[i].argb = color;
	}
	render_quad(texture_index);
}

// ---------------------------------------------------------------------------
// 2D

void render_push_2d(vec2i_t pos, vec2i_t size, rgba_t color, uint16_t texture) {
	render_push_2d_tile(pos, vec2i(0, 0), render_texture_size(texture), size, color, texture);
}

void render_push_2d_tile(vec2i_t pos, vec2i_t uv_offset, vec2i_t uv_size, vec2i_t size, rgba_t color, uint16_t texture_index) {
	int saved_test = depth_test, saved_write = depth_write;
	depth_test = 0;
	depth_write = 0;
	sync_depth_state();

	int textured = is_textured(texture_index);
	if (textured) {
		texture_bind(texture_index);
	}

	u64 rgbaq = rgbaq_from(color, textured, 1.0f);
	// From the game's layout height to the 480 lines xyz_from() expects.
	float sy = 480.0f / screen_size.y;
	// UV is in texels, 10.4 fixed point.
	gs_vertex_t corners[2] = {
		{
			.rgbaq = rgbaq,
			.uv = GS_SET_UV(uv_offset.x * 16, uv_offset.y * 16),
			.xyz = xyz_from(pos.x, pos.y * sy, 0),
		},
		{
			.rgbaq = rgbaq,
			.uv = GS_SET_UV((uv_offset.x + uv_size.x) * 16, (uv_offset.y + uv_size.y) * 16),
			.xyz = xyz_from(pos.x + size.x, (pos.y + size.y) * sy, 0),
		},
	};
	gs_draw(GS_SET_PRIM(GS_PRIM_SPRITE, 0, textured, 0, 1, 0, 1, 0, 0), corners, 2);

	depth_test = saved_test;
	depth_write = saved_write;
}


// ---------------------------------------------------------------------------
// Textures
//
// Every texture is kept in RAM, padded to powers of two as on the Dreamcast
// (game code divides UVs by render_texture_padsize()), in the GS's 16-bit
// colour order. The VRAM after gsKit's frame and Z buffers is a cache: a
// texture is uploaded the first time it's drawn, and when the cache is full
// it's emptied and filled again from RAM.

typedef struct {
	vec2i_t size;     // as the game made it
	vec2i_t padsize;  // powers of two, as uploaded
	uint16_t *pixels; // padsize, PSMCT16; NULL for RENDER_NO_TEXTURE
	int32_t vram;     // block address (256-byte units), or -1 if not in VRAM
	uint16_t blocks;  // VRAM blocks the texture covers
	uint8_t unfiltered;
} texture_t;

static texture_t textures[TEXTURES_MAX];
static uint16_t textures_len;

#define VRAM_BLOCKS (4 * 1024 * 1024 / 256)
static uint32_t vram_start; // first block after gsKit's buffers
static uint32_t vram_next;

static int32_t sent_texture = -1;
static u64 sent_tex1 = ~0ull;

// Upload counts, logged once a second.
static uint32_t stat_frames, stat_uploads, stat_bytes, stat_resets;

// Block numbers within a 64x64-pixel PSMCT16 page, by block row and column
// (blocks are 16x8 pixels).
static const uint8_t block_ct16[8][4] = {
	{ 0,  2,  8, 10},
	{ 1,  3,  9, 11},
	{ 4,  6, 12, 14},
	{ 5,  7, 13, 15},
	{16, 18, 24, 26},
	{17, 19, 25, 27},
	{20, 22, 28, 30},
	{21, 23, 29, 31},
};

// Buffer width in 64-pixel units.
static int texture_tbw(int width) {
	return width < 64 ? 1 : width / 64;
}

// The highest block a w x h PSMCT16 texture touches, plus one. A texture
// narrower or shorter than a page doesn't use a contiguous run of blocks.
static uint16_t texture_blocks(int w, int h) {
	int tbw = texture_tbw(w);
	int max = 0;
	for (int y = 0; y < h; y += 8) {
		for (int x = 0; x < w; x += 16) {
			int page = (y / 64) * tbw + x / 64;
			int block = page * 32 + block_ct16[(y % 64) / 8][(x % 64) / 16];
			if (block > max) {
				max = block;
			}
		}
	}
	return max + 1;
}

static void textures_vram_init(void) {
	vram_start = (gs->CurrentPointer + 255) / 256;
	vram_next = vram_start;
	printf("textures: %u KB of VRAM from 0x%x\n",
		(unsigned)(VRAM_BLOCKS - vram_start) / 4, (unsigned)vram_start * 256);
}

static void textures_vram_clear(void) {
	for (int i = 0; i < textures_len; i++) {
		textures[i].vram = -1;
	}
	vram_next = vram_start;
	sent_texture = -1;
}

static void texture_upload(texture_t *t) {
	if (vram_next + t->blocks > VRAM_BLOCKS) {
		textures_vram_clear();
		stat_resets++;
	}
	t->vram = vram_next;
	vram_next += t->blocks;

	uint32_t bytes = t->padsize.x * t->padsize.y * 2;
	gs_upload(t->vram, texture_tbw(t->padsize.x), GS_PSM_CT16, t->padsize.x, t->padsize.y, t->pixels, bytes);
	gs_set_reg(GS_REG_TEXFLUSH, 0);
	sent_texture = -1;

	stat_uploads++;
	stat_bytes += bytes;
}

static int is_textured(uint16_t texture_index) {
	return texture_index < textures_len && textures[texture_index].pixels != NULL;
}

static void texture_bind(uint16_t texture_index) {
	texture_t *t = &textures[texture_index];
	if (t->vram < 0) {
		texture_upload(t);
	}
	if (texture_index != sent_texture) {
		// RGBA (alpha from the texture), modulated by the vertex colour
		gs_set_reg(GS_REG_TEX0, GS_SET_TEX0(t->vram, texture_tbw(t->padsize.x), GS_PSM_CT16,
			__builtin_ctz(t->padsize.x), __builtin_ctz(t->padsize.y), 1, 0, 0, 0, 0, 0, 0));
		sent_texture = texture_index;
	}
	int filter = save.filter && !t->unfiltered; // 1 = bilinear
	u64 tex1 = GS_SET_TEX1(0, 0, filter, filter, 0, 0, 0);
	if (tex1 != sent_tex1) {
		gs_set_reg(GS_REG_TEX1, tex1);
		sent_tex1 = tex1;
	}
}

static void textures_frame_start(void) {
	// A 16-bit texel's alpha bit becomes alpha 0 or 0x80.
	gs_set_reg(GS_REG_TEXA, GS_SET_TEXA(0x00, 0, 0x80));
	gs_set_reg(GS_REG_CLAMP, GS_SET_CLAMP(0, 0, 0, 0, 0, 0)); // repeat
	sent_texture = -1;
	sent_tex1 = ~0ull;
}

static void textures_frame_end(void) {
	if (++stat_frames < 60) {
		return;
	}
	if (stat_uploads) {
		printf("textures: %u uploads (%u KB), %u cache resets in %u frames\n",
			(unsigned)stat_uploads, (unsigned)stat_bytes / 1024, (unsigned)stat_resets,
			(unsigned)stat_frames);
	}
	stat_frames = stat_uploads = stat_bytes = stat_resets = 0;
}

// next power of 2 greater than or equal to v, at least 16 (as on the Dreamcast)
static uint32_t np2(uint32_t v) {
	if (v < 16) {
		return 16;
	}
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	return v + 1;
}

// The game's ARGB1555 to the GS's ABGR1555.
static uint16_t gs_color16(uint16_t c) {
	return (c & 0x83e0) | ((c >> 10) & 0x1f) | ((c & 0x1f) << 10);
}

uint16_t render_texture_create(uint32_t width, uint32_t height, uint16_t *pixels) {
	if (textures_len >= TEXTURES_MAX) {
		return RENDER_NO_TEXTURE;
	}
	uint16_t texture_index = textures_len++;
	texture_t *t = &textures[texture_index];
	*t = (texture_t){
		.size = vec2i(width, height),
		.padsize = vec2i(width, height),
		.vram = -1,
		.unfiltered = render_state.LOAD_UNFILTERED,
	};
	if (pixels == NULL) {
		return texture_index;
	}

	// The Dreamcast gives empty textures (a ring on the stopwatch model) a
	// transparent 8x8 one.
	int empty = (width == 0 || height == 0);
	if (empty) {
		width = height = 8;
		t->size = vec2i(width, height);
	}

	uint32_t pw = np2(width), ph = np2(height);
	t->padsize = vec2i(pw, ph);
	t->blocks = texture_blocks(pw, ph);
	t->pixels = memalign(64, pw * ph * 2);
	uint16_t *dst = t->pixels;

	if (empty) {
		memset(dst, 0, pw * ph * 2);
		return texture_index;
	}

	// Pad with repeated columns and rows, so wrapping and filtering at the
	// edges see the texture itself.
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			dst[y * pw + x] = gs_color16(pixels[y * width + x]);
		}
		for (uint32_t x = width; x < pw; x++) {
			dst[y * pw + x] = dst[y * pw + x - width];
		}
	}
	for (uint32_t y = height; y < ph; y++) {
		memcpy(&dst[y * pw], &dst[(y - height) * pw], pw * 2);
	}
	return texture_index;
}

vec2i_t render_texture_size(uint16_t texture_index) {
	if (texture_index >= textures_len) {
		return vec2i(0, 0);
	}
	return textures[texture_index].size;
}

vec2i_t render_texture_padsize(uint16_t texture_index) {
	if (texture_index >= textures_len) {
		return vec2i(0, 0);
	}
	return textures[texture_index].padsize;
}

// Only the intro video uses this, which isn't ported.
void render_texture_replace_pixels(int16_t texture_index, uint16_t *pixels) {}

uint16_t render_textures_len(void) {
	return textures_len;
}

void render_textures_reset(uint16_t len) {
	for (int i = len; i < textures_len; i++) {
		free(textures[i].pixels);
		textures[i].pixels = NULL;
	}
	textures_len = len;
	textures_vram_clear();

	// Index 0 is always RENDER_NO_TEXTURE.
	if (len == 0) {
		RENDER_NO_TEXTURE = render_texture_create(2, 2, NULL);
	}
}

void render_textures_dump(const char *path) {}
