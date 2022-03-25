#pragma once

#include <immintrin.h>

#ifndef ADD_SIMD_TARGET
#if defined(__GNUC__) || defined(__llvm__)
#define ADD_SIMD_TARGET __attribute__((target("sse4.1")))
#else
#define ADD_SIMD_TARGET
#endif
#endif

typedef __m128i ts_int32x4;

// SSE2
#define ts_load_i32(addr) _mm_load_si128(addr)

// SSE2
#define ts_init_i32(a, b, c, d) _mm_set_epi32(d, c, b, a)

// SSE2
#define ts_add_i32(a, b) _mm_add_epi32(a, b)

// SSE2
#define ts_sub_i32(a, b) _mm_sub_epi32(a, b)

// SSE4.1 (note: latency 10)
#define ts_mul_i32(a, b) _mm_mullo_epi32(a, b)

// SSE2 (note: latency 5-6)
static inline int32_t ts_hadd_i32(ts_int32x4 val) {
	// 2x _mm_hadd_epi32 is slower in testing, these 5 instructions are faster
	val = _mm_add_epi32(val, _mm_shuffle_epi32(val, _MM_SHUFFLE(1, 0, 3, 2)));
	val = _mm_add_epi32(val, _mm_shuffle_epi32(val, _MM_SHUFFLE(2, 3, 0, 1)));
	return (int32_t) _mm_cvtsi128_si32(val);
}

// SSE2
#define ts_and_u32(a, b) _mm_and_si128(a, b)

// SSE2
#define ts_or_u32(a, b) _mm_or_si128(a, b)

// SSSE3
#define ts_shuffle_u8(a, b) _mm_shuffle_epi8(a, b)
