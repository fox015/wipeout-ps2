#ifndef MTX_PS2_H
#define MTX_PS2_H

// PS2 implementation of mtx.h: the current matrix lives in memory, in plain C.
// Column-major like the SH4's XMTRX: x' = m[0]*x + m[4]*y + m[8]*z + m[12]*w.
// Later this can move to VU0 registers.

extern mat4_t mtx_current;

static inline void mtx_load(const mat4_t *m) {
	mtx_current = *m;
}

void mtx_load_mul(const mat4_t *a, const mat4_t *b);

#define mtx_trans3(x, y, z) do { \
	const float *mtx_m = mtx_current.m; \
	float mtx_x = (x), mtx_y = (y), mtx_z = (z); \
	float mtx_rw = 1.0f / (mtx_m[3] * mtx_x + mtx_m[7] * mtx_y + mtx_m[11] * mtx_z + mtx_m[15]); \
	(x) = (mtx_m[0] * mtx_x + mtx_m[4] * mtx_y + mtx_m[8]  * mtx_z + mtx_m[12]) * mtx_rw; \
	(y) = (mtx_m[1] * mtx_x + mtx_m[5] * mtx_y + mtx_m[9]  * mtx_z + mtx_m[13]) * mtx_rw; \
	(z) = (mtx_m[2] * mtx_x + mtx_m[6] * mtx_y + mtx_m[10] * mtx_z + mtx_m[14]) * mtx_rw; \
} while (0)

#define dot4(a, b, c, d, e, f, g, h) ((a) * (e) + (b) * (f) + (c) * (g) + (d) * (h))

static inline float vec3_dot(vec3_t a, vec3_t b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline float vec3_len_sq(vec3_t a) {
	return vec3_dot(a, a);
}

static inline float vec3_len(vec3_t a) {
	return sqrtf(vec3_len_sq(a));
}

static inline vec3_t vec3_normalize(vec3_t a) {
	float factor = 1.0f / vec3_len(a);
	return vec3(a.x * factor, a.y * factor, a.z * factor);
}

#endif
