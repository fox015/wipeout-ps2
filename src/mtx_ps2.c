#include <string.h>

#include "mtx.h"

mat4_t mtx_current;

// current = a * b
void mtx_load_mul(const mat4_t *a, const mat4_t *b) {
	mat4_t r;
	for (int col = 0; col < 4; col++) {
		for (int row = 0; row < 4; row++) {
			r.cols[col][row] =
				a->cols[0][row] * b->cols[col][0] +
				a->cols[1][row] * b->cols[col][1] +
				a->cols[2][row] * b->cols[col][2] +
				a->cols[3][row] * b->cols[col][3];
		}
	}
	mtx_current = r;
}

// Matches asmfuncs.S: output = current * (x, y, z, 1) with w kept, and the
// result is 1 only if every vertex has z > -w (in front of the near plane).
int xform_all_verts(vec4_t *output, vec4_t *input, int count) {
	const float *m = mtx_current.m;
	int wtest = 1;
	for (int i = 0; i < count; i++) {
		float x = input[i].x, y = input[i].y, z = input[i].z;
		output[i].x = m[0] * x + m[4] * y + m[8]  * z + m[12];
		output[i].y = m[1] * x + m[5] * y + m[9]  * z + m[13];
		output[i].z = m[2] * x + m[6] * y + m[10] * z + m[14];
		output[i].w = m[3] * x + m[7] * y + m[11] * z + m[15];
		wtest &= output[i].z > -output[i].w;
	}
	return wtest;
}

void *memcpy32(void *restrict dst, const void *restrict src, size_t bytes) {
	return memcpy(dst, src, bytes);
}
