#ifndef MTX_H
#define MTX_H

// The "current matrix": load a matrix once, then transform many vectors
// against it. The Dreamcast keeps it in the SH4's XMTRX register bank. The
// PS2 keeps it in C first, and later in VU0 registers, which work the same way.
//
//   mtx_load(m)              current = m
//   mtx_load_mul(a, b)       current = a * b
//   mtx_trans3(x, y, z)      transform (x, y, z, 1) by current and divide by w,
//                            in place (x, y and z must be lvalues)
//   vec3_dot(a, b)           dot product
//   vec3_len(v)              length of v
//   vec3_len_sq(v)           squared length of v
//   vec3_normalize(v)        v scaled to length 1
//   xform_all_verts(o, i, n) transform n object vertices by current into o;
//                            returns 1 if all are in front of the near plane
//   memcpy32(d, s, n)        fast copy: d 32-byte aligned, n a multiple of 32

#include "types.h"

#if defined(_arch_dreamcast)
#include "mtx_dc.h"
#else
#error "mtx.h: no matrix implementation for this platform"
#endif

int xform_all_verts(vec4_t *output, vec4_t *input, int count);
void *memcpy32(void *restrict dst, const void *restrict src, size_t bytes);

#endif
