#pragma once

#include <stdint.h>

// TODO: put this somewhere better
#if defined(__linux__) || defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#include <endian.h>
#else
#if defined(__APPLE__)
#include <machine/endian.h>
#endif
#endif
#ifndef __BYTE_ORDER
/**
 * \def __LITTLE_ENDIAN
 * Little-endian byte order.
 */
#define __LITTLE_ENDIAN 1234
/**
 * \def __BIG_ENDIAN
 * Big-endian byte order.
 */
#define __BIG_ENDIAN    4321
/*
 * For x86/x64 we can simply set this manually For ARM32/64 this could
 * (theoretically) be big-endian; unlikely given the target platforms.
 *
 * \todo assume LE unless it's a known BE? Yes.
 */
#if defined(__i386__) || defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__arm__) || defined(_M_ARM) || defined(__aarch64__) || defined(_M_ARM64)
#define __BYTE_ORDER  __LITTLE_ENDIAN
#else
#if defined(__ppc__) || defined(_M_PPC) || defined(__ppc64__)
#define __BYTE_ORDER  __BIG_ENDIAN
#endif
#endif
#endif

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
