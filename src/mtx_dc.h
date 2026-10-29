#ifndef MTX_DC_H
#define MTX_DC_H

// Dreamcast implementation of mtx.h: the SH4's XMTRX register bank.
// xform_all_verts and memcpy32 are in asmfuncs.S.

#include <kos.h>

// thanks @FalcoGirgis
inline static void mtx_load(const mat4_t *m) {
    const matrix_t *mtx = &m->cols;
    asm volatile(
        R"(
            fschg
            fmov.d    @%[mtx],xd0
            add        #32,%[mtx]
            pref    @%[mtx]
            add        #-(32-8),%[mtx]
            fmov.d    @%[mtx]+,xd2
            fmov.d    @%[mtx]+,xd4
            fmov.d    @%[mtx]+,xd6
            fmov.d    @%[mtx]+,xd8
            fmov.d    @%[mtx]+,xd10
            fmov.d    @%[mtx]+,xd12
            fmov.d    @%[mtx]+,xd14
            fschg
        )"
        : [mtx] "+r" (mtx)
        :
        :
    );
}

// thanks @FalcoGirgis
inline static void mtx_load_mul(const mat4_t *a, const mat4_t *b) {
    const matrix_t *matrix1 = &a->cols;
    const matrix_t *matrix2 = &b->cols;
    unsigned int prefetch_scratch;

    asm volatile (
        "mov %[bmtrx], %[pref_scratch]\n\t"
        "add #32, %[pref_scratch]\n\t"
        "fschg\n\t"
        "pref @%[pref_scratch]\n\t"
        // back matrix
        "fmov.d @%[bmtrx]+, XD0\n\t"
        "fmov.d @%[bmtrx]+, XD2\n\t"
        "fmov.d @%[bmtrx]+, XD4\n\t"
        "fmov.d @%[bmtrx]+, XD6\n\t"
        "pref @%[fmtrx]\n\t"
        "fmov.d @%[bmtrx]+, XD8\n\t"
        "fmov.d @%[bmtrx]+, XD10\n\t"
        "fmov.d @%[bmtrx]+, XD12\n\t"
        "mov %[fmtrx], %[pref_scratch]\n\t"
        "add #32, %[pref_scratch]\n\t"
        "fmov.d @%[bmtrx], XD14\n\t"
        "pref @%[pref_scratch]\n\t"
        // front matrix
        // interleave loads and matrix multiply 4x4
        "fmov.d @%[fmtrx]+, DR0\n\t"
        "fmov.d @%[fmtrx]+, DR2\n\t"
        "fmov.d @%[fmtrx]+, DR4\n\t"
        "ftrv XMTRX, FV0\n\t"

        "fmov.d @%[fmtrx]+, DR6\n\t"
        "fmov.d @%[fmtrx]+, DR8\n\t"
        "ftrv XMTRX, FV4\n\t"

        "fmov.d @%[fmtrx]+, DR10\n\t"
        "fmov.d @%[fmtrx]+, DR12\n\t"
        "ftrv XMTRX, FV8\n\t"

        "fmov.d @%[fmtrx], DR14\n\t"
        "fschg\n\t"
        "ftrv XMTRX, FV12\n\t"
        "frchg\n"
        : [bmtrx] "+&r" ((unsigned int)matrix1), [fmtrx] "+r" ((unsigned int)matrix2), [pref_scratch] "=&r" (prefetch_scratch)
        : // no inputs
        : "fr0", "fr1", "fr2", "fr3", "fr4", "fr5", "fr6", "fr7", "fr8", "fr9", "fr10", "fr11", "fr12", "fr13", "fr14", "fr15"
    );
}

#define mtx_trans3(x, y, z) mat_trans_single3(x, y, z)

#define vec3_len_sq(v) vec_fipr(v)

#endif
