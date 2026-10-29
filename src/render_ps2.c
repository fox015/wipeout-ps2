// PS2 renderer, using gsKit for now.
// Stage 3: 2D only, untextured. Textured 2D draws get a colour derived from
// their texture index, so each element's layout is visible. 3D is stage 5.
// Textures only record their size, which the UI needs for layout.

#include <gsKit.h>
#include <dmaKit.h>

#include "render.h"
#include "todo_ps2.h"

#define TEXTURES_MAX 1024

global_render_state_t render_state;
render_vertex_t vs[5];
uint16_t RENDER_NO_TEXTURE;
const uint16_t HUD_NO_TEXTURE = 65535;

// hud.c projects the target reticle with these
mat4_t view_mat = mat4_identity();
mat4_t mvp_mat = mat4_identity();
mat4_t vp_mat = mat4_identity();

static GSGLOBAL *gs;

#if defined(VIDEO_480p)
#define defined_video_mode GS_MODE_DTV_480P
#elif defined(VIDEO_vga)
#define defined_video_mode GS_MODE_VGA_640_60
#endif

// The game lays out a 640x480 screen; the GS output may have fewer lines.
static vec2i_t screen_size = {640, 480};
static float scale_y = 1.0f;
static vec2i_t texture_sizes[TEXTURES_MAX];
static uint16_t textures_len;

void render_init(void) {
	gs = gsKit_init_global();
	gs->PSM = GS_PSM_CT32;
	gs->PSMZ = GS_PSMZ_16S;
	gs->ZBuffering = GS_SETTING_OFF;
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

	// Normal alpha blending: (Cs - Cd) * As + Cd. gsKit's default,
	// GS_BLEND_BACK2FRONT, keeps the destination for opaque pixels, so
	// nothing drawn would show.
	gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
	scale_y = (float)gs->Height / screen_size.y;
	printf("GS: %dx%d, interlace %d, field %d\n", gs->Width, gs->Height, gs->Interlace, gs->Field);

	RENDER_NO_TEXTURE = render_texture_create(2, 2, NULL);
}

// rgba_t to a GS colour: GS alpha 0x80 is fully opaque.
static u64 gs_color(rgba_t c) {
	return GS_SETREG_RGBAQ(c.r, c.g, c.b, (c.a + 1) >> 1, 0x00);
}

// Stand-in for a texture until stage 6: a fixed colour per texture index.
static rgba_t texture_debug_color(uint16_t texture_index) {
	uint32_t h = texture_index * 2654435761u;
	return rgba(64 + (h >> 24) % 160, 64 + (h >> 16) % 160, 64 + (h >> 8) % 160, 255);
}

static rgba_t rgba_mul(rgba_t a, rgba_t b) {
	return rgba(a.r * b.r / 255, a.g * b.g / 255, a.b * b.b / 255, a.a * b.a / 255);
}

void render_cleanup(void) { TODO(); }

vec2i_t render_res_to_size(render_resolution_t res) {
	return res == RENDER_RES_43 ? vec2i(640, 480) : vec2i(640, 360);
}

void render_reset_proj(float farval) { TODO(); }
void render_set_screen_size(vec2i_t size) { screen_size = size; }
void render_set_resolution(render_resolution_t res) { TODO(); }
void render_set_post_effect(render_post_effect_t post) { TODO(); }
vec2i_t render_size(void) { return screen_size; }

void render_frame_prepare(void) {
	gsKit_clear(gs, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x80, 0x00));
}

// Waits for vsync, so the main loop runs at the display rate.
void render_frame_end(void) {
	gsKit_sync_flip(gs);
	gsKit_queue_exec(gs);
	gsKit_queue_reset(gs->Os_Queue);
}
void render_begin_translucent(void) { TODO(); }

void render_set_view(vec3_t pos, vec3_t angles) { TODO(); }
void render_set_view_2d(void) { TODO(); }
void render_set_model_mat(mat4_t *m) { TODO(); }
void render_set_model_ident(void) { TODO(); }
void render_set_depth_write(bool enabled) { TODO(); }
void render_set_depth_test(bool enabled) { TODO(); }
void render_set_depth_offset(float offset) { TODO(); }
void render_set_screen_position(vec2_t pos) { TODO(); }
void render_set_blend_mode(render_blend_mode_t mode) { TODO(); }
void render_set_cull_backface(bool enabled) { TODO(); }

void render_hud_quad(uint16_t texture) { TODO(); }

// Screen shake from weapon hits
void SetShake(float duration) { TODO(); }
void ShakeScreen(void) { TODO(); }

void render_quad(uint16_t texture) { TODO(); }
void render_tri(uint16_t texture) { TODO(); }
void render_quad_noxform(uint16_t texture_index, float *w) { TODO(); }
void render_tri_noxform(uint16_t texture_index, float *w) { TODO(); }
void render_quad_noxform_noclip(uint16_t texture_index, float *w) { TODO(); }
void render_tri_noxform_noclip(uint16_t texture_index, float *w) { TODO(); }

void render_push_sprite(vec3_t pos, vec2i_t size, uint32_t color, uint16_t texture) { TODO(); }
void render_push_2d(vec2i_t pos, vec2i_t size, rgba_t color, uint16_t texture) {
	render_push_2d_tile(pos, vec2i(0, 0), render_texture_size(texture), size, color, texture);
}

void render_push_2d_tile(vec2i_t pos, vec2i_t uv_offset, vec2i_t uv_size, vec2i_t size, rgba_t color, uint16_t texture_index) {
	if (texture_index != RENDER_NO_TEXTURE) {
		color = rgba_mul(color, texture_debug_color(texture_index));
	}
	gsKit_prim_sprite(gs,
		pos.x, pos.y * scale_y,
		pos.x + size.x, (pos.y + size.y) * scale_y,
		0, gs_color(color));
}

uint16_t render_texture_create(uint32_t width, uint32_t height, uint16_t *pixels) {
	TODO();
	if (textures_len >= TEXTURES_MAX) {
		return 0;
	}
	texture_sizes[textures_len] = vec2i(width, height);
	return textures_len++;
}

vec2i_t render_texture_size(uint16_t texture_index) {
	return texture_sizes[texture_index];
}

vec2i_t render_texture_padsize(uint16_t texture_index) {
	return texture_sizes[texture_index];
}

void render_texture_replace_pixels(int16_t texture_index, uint16_t *pixels) { TODO(); }

uint16_t render_textures_len(void) {
	return textures_len;
}

void render_textures_reset(uint16_t len) {
	textures_len = len;
}

void render_textures_dump(const char *path) { TODO(); }
