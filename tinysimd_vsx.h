#pragma once

#include <altivec.h>

/*
 * To solve: disable 'Java mode' in the Vector Status and Control Register (set
 * VSCR[NJ] = 1) and allow it to be restored afterwards. E.g. grab vec_mfvscr(),
 * save it for restoring later, set the NJ bit, then update with vec_mtvscr().
 * It's a vector of unsigned shorts, with elements 7 & 8 containing the register
 * values.
 *
 * I *think* this is the right thing since Neon doesn't support denormal or NaN
 * inputs or results, and this would make VSX behave the same.
 */

#ifndef ADD_SIMD_TARGET
#define ADD_SIMD_TARGET
#endif

typedef __vector int ts_int32x4;

 // load/store

#define ts_load_i32(addr) vec_ld(0, (const int32_t*) addr)

#define ts_init_i32(a, b, c, d) (ts_int32x4) {(int32_t) a, (int32_t) b, (int32_t) c, (int32_t) d}

static inline int32_t ts_lane_u32(ts_int32x4 v, unsigned n) {
	union {
		ts_int32x4 v;
		int32_t i[4];
	} vec = {v};
	return vec.i[n];
}

// arithmetic

#define ts_add_i32(a, b) vec_add(a, b)

#define ts_sub_i32(a, b) vec_sub(a, b)

#define ts_mul_i32(a, b) vec_mul(a, b)

static inline int32_t ts_hadd_i32(ts_int32x4 val) {
	/*
	 * This is six instructions, but on Power8 at least the lane extract is a
	 * 'mfvsrwz' vector to scalar register, rather than to memory.
	 */
	return ts_lane_u32(vec_sums(val, vec_splat_s32(0)), 3);
}

// logical

static inline ts_int32x4 ts_not_u32(ts_int32x4 val) {
	return vec_xor(val, vec_splat_s32(-1));
}

#define ts_and_u32(a, b) vec_and(a, b)

#define ts_or_u32(a, b) vec_or(a, b)

#define ts_xor_u32(a, b) vec_xor(a, b)

// shuffles

#define ts_shuffle_u8(a, b) vec_perm(a, vec_splat_s32(0), (__vector unsigned char) b)
