// Extract from basisu_transcoder.cpp
// Copyright (C) 2019-2021 Binomial LLC. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

//************************** Helpers and Boilerplate **************************/

#include "basisu_headers.h"

#include "tinysimd.h"

/**
 * Helper to return the current time in milliseconds.
 */
static unsigned millis() {
	return static_cast<unsigned>((clock() * 1000LL) / CLOCKS_PER_SEC);
}

/**
 * Prebuilt table with known results.
 */
static const etc1_to_dxt1_56_solution known[32 * 8 * NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS * NUM_ETC1_TO_DXT1_SELECTOR_RANGES] = {
#include "basisu_transcoder_tables_dxt1_6.inc"
};

/**
 * Helper to compare two tables to see if they match.
 */
static bool verifyTable(const etc1_to_dxt1_56_solution* a, const etc1_to_dxt1_56_solution* b) {
	for (unsigned n = 0; n < 32 * 8 * NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS * NUM_ETC1_TO_DXT1_SELECTOR_RANGES; n++) {
		if (a->m_hi != b->m_hi || a->m_lo != b->m_lo || a->m_err != b->m_err) {
			printf("Failed with n = %d\n", n);
			return false;
		}
		a++;
		b++;
	}
	return true;
}

//************************ Optimisation Task Goes Here ************************/

/**
 * Results stored here.
 */
static etc1_to_dxt1_56_solution result[32 * 8 * NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS * NUM_ETC1_TO_DXT1_SELECTOR_RANGES];

// Helper to assign an array[4] as a compound literal
struct Vec4 {
	uint32_t a;
	uint32_t b;
	uint32_t c;
	uint32_t d;
};

// Helper to access a Vec4 as an array
typedef uint32_t Vec4Int[4];

/*
 * Note: the original code has two very similar functions to generate the 5- and
 * 6-bit tables, so in the design process this was rewritten as a template.
 *
 * TODO: hmm, something's not right, ARM can get to 59ms with just the colour table, so why is this only reaching 41ms
 * TODO: removing the table and calculating per loop means the Neon SIMD implementation is slower than scalar with a table (86ms)
 *
 * Note: Win/SSE on Mac Xeon is getting around 92ms (vs Wasm on the same machine
 * with 112ms!) but this is slower than the earlier SSE-only experiment, the
 * reason being is the older one used epi32 immediate shuffles unrolled, which
 * isn't very versatile (epi8 shuffles let us programmatically define the
 * shuffles, whereas epi32 is compile-time). 92ms is terrible compare with the
 * 37ms on an M1 Mac!
 */
template<unsigned Bits>
ADD_SIMD_TARGET
static void create_etc1_to_dxt1_conversion_table_simd() {
	etc1_to_dxt1_56_solution* dst = result;
	/*
	 * Easy first choice: Pre-calculate the endpoint colours. There are 4096
	 * (for a 6-bit endpoint) and these same calculations were run 15360x in the
	 * original implementation (intensities * greens * ranges * mappings). This
	 * alone on an M1 results in a 2.6x speed-up (158ms to 59ms); on a Xeon this
	 * isn't so impressive, resulting in only a 30% improvement.
	 * 
	 * TODO: aligned malloc this, instead of putting 16kB on the stack (so vec_malloc(), _mm_malloc(), _aligned_malloc(), aligned_alloc(), posix_memalign(), etc.)
	 */
	ALIGNED_VAR(Vec4, 16) colorTable[(1 << Bits) * (1 << Bits)];
	Vec4* nextVec4 = colorTable;
	for (uint32_t hi = 0; hi < (1 << Bits); hi++) {
		uint32_t hi8 = (hi << (8 - Bits)) | (hi >> (Bits - (8 - Bits)));
		for (uint32_t lo = 0; lo < (1 << Bits); lo++) {
			uint32_t lo8 = (lo << (8 - Bits)) | (lo >> (Bits - (8 - Bits)));
			*nextVec4++ = (Vec4) {lo8, (lo8 * 2 + hi8) / 3, (hi8 * 2 + lo8) / 3, hi8};
		}
	}
	/*
	 * This is questionable whether it's worth extracting, since using a mask to
	 * implement the selector ranges, then a shuffle for the mapping, is the
	 * real improvement (since we remove a bunch of loops and reads) but since
	 * we're changing how this is done we might as well cache the results.
	 */
	ALIGNED_VAR(Vec4Int, 16) rangeTable[NUM_ETC1_TO_DXT1_SELECTOR_RANGES] = {};
	Vec4Int* nextInt4 = rangeTable;
	for (uint32_t sr = 0; sr < NUM_ETC1_TO_DXT1_SELECTOR_RANGES; sr++) {
		const uint32_t low_selector  = g_etc1_to_dxt1_selector_ranges[sr].m_low;
		const uint32_t high_selector = g_etc1_to_dxt1_selector_ranges[sr].m_high;
		for (uint32_t s = low_selector; s <= high_selector; s++) {
			(*nextInt4)[s] = 0xFFFFFFFF;
		}
		nextInt4++;
	}
	/*
	 * We use these shuffle values to move whole ints, more of which below,
	 * except the last which zeroes the destination (-1 fulfils bit-7 on SSE
	 * and OOB on Neon, Wasm and VSX). The 8-bit shuffle is overkill, and the
	 * equivalent of SSE2's _mm_shuffle_epi32() would be preferred, but it takes
	 * immediate values, so we'd need to specify them at compile time (or use a
	 * switch).
	 */
	uint32_t const shuffle8[5] = {
		0x03020100, //  3,  2,  1,  0
		0x07060504, //  7,  6,  5,  4
		0x0B0A0908, // 11, 10,  9,  8
		0x0F0E0D0C, // 15, 14, 13, 12
		0xFFFFFFFF, // -1, -1, -1, -1
	};
	/*
	 * With the above shuffles we create another table to move colour table
	 * entries on the selector mappings. Combined with 'rangeMask' this now
	 * fully eliminates the very expensive colour error calculation loop.
	 */
	ALIGNED_VAR(Vec4Int, 16) mappingTable[NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS];
	nextInt4 = mappingTable;
	for (uint32_t m = 0; m < NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS; m++) {
		const uint8_t* selectorMapping = g_etc1_to_dxt1_selector_mappings[m];
		(*nextInt4)[0] = shuffle8[selectorMapping[0]];
		(*nextInt4)[1] = shuffle8[selectorMapping[1]];
		(*nextInt4)[2] = shuffle8[selectorMapping[2]];
		(*nextInt4)[3] = shuffle8[selectorMapping[3]];
		nextInt4++;
	}
	/*
	 * Calculations start here. The colour table is a good start but the main
	 * aim is to remove branches. Taking this tables above most of the inner
	 * loop finding the lowest error can be reduced to one branch.
	 */
	for (int inten = 0; inten < 8; inten++) {
		for (uint32_t g = 0; g < 32; g++) {
			/*
			 * We *could* optimise this, since it's extra work for just the
			 * green channel, which then need repacking manually into a vector.
			 * But it only runs 256x so it's not worth the effort (of writing a
			 * cut-down get_diff_subblock_colors/pack_color5). 'allColors'
			 * holds the original calculated block green channels which are
			 * later masked out and variations made.
			 * 
			 * TODO: go on, take out the calls, you'll sleep better
			 */
			color32 block_colors[4];
			decoder_etc_block::get_diff_subblock_colors(block_colors, decoder_etc_block::pack_color5(color32(g, g, g, 255), false), inten);
			ts_int32x4 const allColors = ts_init_i32(block_colors[0].g, block_colors[1].g, block_colors[2].g, block_colors[3].g);
			for (uint32_t sr = 0; sr < NUM_ETC1_TO_DXT1_SELECTOR_RANGES; sr++) {
				/*
				 * We get the pre-calculated range mask and apply it to the
				 * block. Afterwards the inverted mask is prepared (see below).
				 */
				ts_int32x4 rangeMask = ts_load_i32(rangeTable[sr]);
				ts_int32x4 const usedColors = ts_and_u32(allColors, rangeMask);
				rangeMask = ts_not_u32(rangeMask);
				for (uint32_t m = 0; m < NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS; m++) {
					/*
					 * The mapping table knows the shuffles but needs the range
					 * also applying to zero-out unwanted entries. OR'ing the
					 * inverted range will set these entries to -1, which being
					 * OOB will zero them out when shuffling.
					 */
					ts_int32x4 const mapping = ts_or_u32(ts_load_i32(mappingTable[m]), rangeMask);
					/*
					 * If we merge the two inner loops into one Wasm gets a nice
					 * 20% boost, other platforms not so much. The downside is
					 * having to calculate hi and lo from the (reverse) index.
					 */
					uint32_t best_err = UINT32_MAX;
					uint32_t best_run = 0;
					nextInt4 = (Vec4Int*) colorTable;
					for (uint32_t n = 1 << (Bits + Bits); n > 0 ; n--) {
						// get the next four precalculated interpolated entries
						ts_int32x4 accum = ts_load_i32(nextInt4++);
						// arrange the entries into g_etc1_to_dxt1_selector_mappings order
						accum = ts_shuffle_u8(accum, mapping);
						// calculate the (signed) error differences from the pre-masked used colours
						accum = ts_sub_i32(accum, usedColors);
						// square the errors
						accum = ts_mul_i32(accum, accum);
						// sum all the errors (recalling we've already masked out unused entries)
						uint32_t total_err = ts_hadd_i32(accum);
						// TODO: hint that this is the branch least taken
						if (total_err < best_err) {
							best_err = total_err;
							best_run = n;
							/*
							 * If we take an early-out here once we've hit zero
							 * then some compiler/CPU combinations will get
							 * worse, some better. MSVC/Xeon can lose up to 20%
							 * (though with a merged loop it's about the same),
							 * whereas Clang/ARM sees a 20 improvement, and Wasm
							 * can almost double.
							 */
							 if (best_err == 0) {
							 	goto outer;
							 }
						}
					}
				outer:
					uint32_t runIdx = (1 << (Bits + Bits)) - best_run;
					*dst++ = (etc1_to_dxt1_56_solution) {
						(uint8_t) (runIdx & ((1 << Bits) - 1)),
						(uint8_t) (runIdx >> Bits),
						(uint16_t) best_err};
				} // m
			} // sr
		} // g
	} // inten
}

/**
 * This takes the table from the SIMD example but keeps the remainder of the
 * code the same (so we can see the difference).
 */
template<unsigned Bits>
static void create_etc1_to_dxt1_conversion_table_precalc() {
	etc1_to_dxt1_56_solution* dst = result;
	/*
	 * See create_etc1_to_dxt1_conversion_table_simd()
	 */
	Vec4 colorTable[(1 << Bits) * (1 << Bits)];
	Vec4* nextInt4 = colorTable;
	for (uint32_t hi = 0; hi < (1 << Bits); hi++) {
		uint32_t hi8 = (hi << (8 - Bits)) | (hi >> (Bits - (8 - Bits)));
		for (uint32_t lo = 0; lo < (1 << Bits); lo++) {
			uint32_t lo8 = (lo << (8 - Bits)) | (lo >> (Bits - (8 - Bits)));
			*nextInt4++ = (Vec4) {lo8, (lo8 * 2 + hi8) / 3, (hi8 * 2 + lo8) / 3, hi8};
		}
	}
	/*
	 * The rest is unchanged (apart from grabbing the precalculated colour and
	 * taking the early-out).
	 */
	for (int inten = 0; inten < 8; inten++) {
		for (uint32_t g = 0; g < 32; g++) {
			color32 block_colors[4];
			decoder_etc_block::get_diff_subblock_colors(block_colors, decoder_etc_block::pack_color5(color32(g, g, g, 255), false), inten);

			for (uint32_t sr = 0; sr < NUM_ETC1_TO_DXT1_SELECTOR_RANGES; sr++) {
				const uint32_t low_selector = g_etc1_to_dxt1_selector_ranges[sr].m_low;
				const uint32_t high_selector = g_etc1_to_dxt1_selector_ranges[sr].m_high;

				for (uint32_t m = 0; m < NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS; m++) {
					const uint8_t* mapping = g_etc1_to_dxt1_selector_mappings[m];
					uint32_t best_lo = 0;
					uint32_t best_hi = 0;
					uint32_t best_err = UINT32_MAX;
					Vec4Int* nextColors = (Vec4Int*) colorTable;
					for (uint32_t hi = 0; hi < (1 << Bits); hi++) {
						for (uint32_t lo = 0; lo < (1 << Bits); lo++) {
							uint32_t total_err = 0;

							for (uint32_t s = low_selector; s <= high_selector; s++) {
								int err = block_colors[s].g - (*nextColors)[mapping[s]];

								total_err += err * err;
							}

							if (total_err < best_err) {
								best_err = total_err;
								best_lo = lo;
								best_hi = hi;
								if (best_err == 0) {
									goto outer;
								}
							}
							nextColors++;
						}
					}
				outer:
					assert(best_err <= 0xFFFF);
					*dst++ = (etc1_to_dxt1_56_solution) {(uint8_t) best_lo, (uint8_t) best_hi, (uint16_t) best_err};
				} // m
			} // sr
		} // g
	} // inten
}

/**
 * Original function.
 */
static void create_etc1_to_dxt1_6_conversion_table_original() {
	uint32_t n = 0;

	for (int inten = 0; inten < 8; inten++) {
		for (uint32_t g = 0; g < 32; g++) {
			color32 block_colors[4];
			decoder_etc_block::get_diff_subblock_colors(block_colors, decoder_etc_block::pack_color5(color32(g, g, g, 255), false), inten);

			for (uint32_t sr = 0; sr < NUM_ETC1_TO_DXT1_SELECTOR_RANGES; sr++) {
				const uint32_t low_selector = g_etc1_to_dxt1_selector_ranges[sr].m_low;
				const uint32_t high_selector = g_etc1_to_dxt1_selector_ranges[sr].m_high;

				for (uint32_t m = 0; m < NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS; m++) {
					uint32_t best_lo = 0;
					uint32_t best_hi = 0;
					uint32_t best_err = UINT32_MAX;

					for (uint32_t hi = 0; hi <= 63; hi++) {
						for (uint32_t lo = 0; lo <= 63; lo++) {
							uint32_t colors[4];

							colors[0] = (lo << 2) | (lo >> 4);
							colors[3] = (hi << 2) | (hi >> 4);

							colors[1] = (colors[0] * 2 + colors[3]) / 3;
							colors[2] = (colors[3] * 2 + colors[0]) / 3;

							uint32_t total_err = 0;

							for (uint32_t s = low_selector; s <= high_selector; s++) {
								int err = block_colors[s].g - colors[g_etc1_to_dxt1_selector_mappings[m][s]];

								total_err += err * err;
							}

							if (total_err < best_err) {
								best_err = total_err;
								best_lo = lo;
								best_hi = hi;
							}
						}
					}

					assert(best_err <= 0xFFFF);

					result[n] = (etc1_to_dxt1_56_solution) {(uint8_t) best_lo, (uint8_t) best_hi, (uint16_t) best_err};

					n++;
				} // m
			} // sr
		} // g
	} // inten
}

//******************************** Entry Point ********************************/

typedef void (*timed) ();

/**
 * Run the passed function and display the quickest of the runs.
 */
static void bestRun(timed func, const char* name) {
	// Before we time it we verify the results are correct
	func();
	if (!verifyTable(result, known)) {
		printf("Generated results don't match known values\n");
	}
	// Now time each
	unsigned best = UINT32_MAX;
	for (int n = 20; n > 0; n--) {
		unsigned time = millis();
		func();
		time = millis() - time;
		if (time < best) {
			best = time;
		}
	}
	printf("Best run took %dms (%s)\n", best, (name) ? name : "default");
}

void runTests() {
	bestRun(create_etc1_to_dxt1_6_conversion_table_original, "Original");
	bestRun(create_etc1_to_dxt1_conversion_table_precalc<6>, "Precalc");
	bestRun(create_etc1_to_dxt1_conversion_table_simd<6>,    "SIMD optimised");
}

ADD_SIMD_TARGET
void printInt32x4(ts_int32x4 v) {
	printf("0: %08X, 1: %08X, 2: %08X, 3: %08X\n",
		ts_lane_u32(v, 0),
		ts_lane_u32(v, 1),
		ts_lane_u32(v, 2),
		ts_lane_u32(v, 3));
}

void printInt(int i) {
	printf("i: %08X (%d)\n", i, i);
}

/**
 * Tests the generation and benchmarks it.
 */
int main(int /*argc*/, char* /*argv*/[]) {
	runTests();
	return 0;
}
