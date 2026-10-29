// PS2 renderer: stubs until stage 3 (2D) and stage 5 (3D).
// Draw calls do nothing. Textures only record their size, which the UI
// needs for layout.

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

static vec2i_t screen_size = {640, 480};
static vec2i_t texture_sizes[TEXTURES_MAX];
static uint16_t textures_len;

void render_init(void) {
	TODO();
	RENDER_NO_TEXTURE = render_texture_create(2, 2, NULL);
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

void render_frame_prepare(void) { TODO(); }
void render_frame_end(void) { TODO(); }
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
void render_push_2d(vec2i_t pos, vec2i_t size, rgba_t color, uint16_t texture) { TODO(); }
void render_push_2d_tile(vec2i_t pos, vec2i_t uv_offset, vec2i_t uv_size, vec2i_t size, rgba_t color, uint16_t texture_index) { TODO(); }

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
