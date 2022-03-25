#pragma once

#include <arm_neon.h>

typedef int32x4_t ts_int32x4;

/*
 * \def _init_i32
 * Helper to initialise either a \c int32x4_t or \c uint32x4_t (with no checks
 * performed on the content, so depending on the warning level out-of-bounds
 * parameters may fail). This works around differences in compilers, e.g.:
 * \code
 *    // Clang & GCC
 *    int32x4_t a = {1, 2, 3, 4};
 *    // MSVC
 *    int temp[4] = {1, 2, 3, 4};
 *    int32x4_t b = vld1q_s32(temp);
 *    // With this helper
 *    int32x4_t c = _init_i32(1, 2, 3, 4);
 * \endcode
 * Note: this works on MSCV because the union \c __n128&zwnj;'s first entry is
 * \c n128_u64 (a pair of \c uint64_t&zwnj;s). Clang and GCC accept the C-style
 * designated-initializer syntax.
 */
#ifndef _init_i32
#ifdef _M_ARM64
#define _init_i32(a, b, c, d) {(a) | (((unsigned __int64) (b)) << 32), (c) | (((unsigned __int64) (d)) << 32)}
#else
#define _init_i32(a, b, c, d) {(a),                       (b),         (c),                       (d)        }
#endif
#endif

#define ts_init_i32(a, b, c, d) _init_i32(a, b, c, d)

#define ts_add_i32(a, b) vaddq_s32(a, b)

#define ts_sub_i32(a, b) vsubq_s32(a, b)

#define ts_mul_i32(a, b) vmulq_s32(a, b)

#define ts_hadd_i32(a) vaddvq_s32(a)
