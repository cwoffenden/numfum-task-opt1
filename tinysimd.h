#pragma once

#include <stdint.h>

/**
 * \def ALIGNED_VAR
 * Aligned variable declaration.
 * 
 * \param[n] alignment in bytes (e.g. 16, for 128-bit SIMD types)
 * \param[type] data type (e.g. \c uint32_t)
 */
#ifndef ALIGNED_VAR
#ifdef _MSC_VER
#define ALIGNED_VAR(type, n) __declspec(align(n)) type
#else
#define ALIGNED_VAR(type, n) type __attribute__((aligned(n)))
#endif
#endif

/*
 * Each of the architectures has a separate header.
 */
#if defined(__x86_64__) || defined(_M_X64)
#include "tinysimd_sse4.h"
#else
#if defined(__aarch64__) || defined(_M_ARM64)
#include "tinysimd_neon.h"
#else
#if defined(__wasm_simd128__)
#include "tinysimd_wasm.h"
#else
#if defined(__VSX__) || defined(__ALTIVEC__)
#include "tinysimd_vsx.h"
#else
// include scalar fallback
#endif
#endif
#endif
#endif
