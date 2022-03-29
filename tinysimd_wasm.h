#pragma once

#include <wasm_simd128.h>

#ifndef ADD_SIMD_TARGET
#if defined(__GNUC__) || defined(__llvm__)
#define ADD_SIMD_TARGET __attribute__((target("simd128")))
#else
#error "Unknown compiler (don't know how to compile Wasm)"
#endif
#endif

typedef __i32x4 ts_int32x4;

// load/store

#define ts_load_i32(addr) wasm_v128_load(addr)

#define ts_init_i32(a, b, c, d) wasm_i32x4_make(a, b, c, d)

#define ts_lane_u32(v, n) wasm_i32x4_extract_lane(v, n)

// arithmetic

#define ts_add_i32(a, b) wasm_i32x4_add(a, b)

#define ts_sub_i32(a, b) wasm_i32x4_sub(a, b)

#define ts_mul_i32(a, b) wasm_i32x4_mul(a, b)

static inline int32_t ts_hadd_i32(ts_int32x4 val) {
	// Wasm is missing a 4-lane add; this is a port of the SSE implementation
	val = wasm_i32x4_add(val, wasm_i32x4_shuffle(val, val, 2, 3, 0, 1));
	val = wasm_i32x4_add(val, wasm_i32x4_shuffle(val, val, 1, 0, 3, 2));
	return (int32_t) wasm_i32x4_extract_lane(val, 0);
}

// logical

#define ts_not_u32(a) wasm_v128_not(a)

#define ts_and_u32(a, b) wasm_v128_and(a, b)

#define ts_or_u32(a, b) wasm_v128_or(a, b)

#define ts_xor_u32(a, b) wasm_v128_xor(a, b)

// shuffles

#define ts_shuffle_u8(a, b) wasm_i8x16_swizzle(a, b)
