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

template<unsigned Bits>
static void create_etc1_to_dxt1_6_conversion_table() {
	etc1_to_dxt1_56_solution* dst = result;
	/*
	 * First pre-calculate the endpoint colours. There are 4096 choices (for a
	 * 6-bit endpoint) and these same calculations were run 15360 times in the
	 * original implementation (intensities * greens * ranges * mappings). This
	 * alone on an M1 results in a 2.6x speed-up (158ms to 59ms); on a Xeon this
	 * isn't so impressive, resulting in only a 30% improvement.
	 */
    ALIGNED_VAR(uint32_t, 16) colorTable[(1 << Bits) * (1 << Bits)][4];
	uint32_t (*entry)[4] = colorTable;
	for (uint32_t hi = 0; hi < (1 << Bits); hi++) {
		uint32_t hi8 = (hi << (8 - Bits)) | (hi >> (Bits - (8 - Bits)));
		for (uint32_t lo = 0; lo < (1 << Bits); lo++) {
			int32_t lo8 = (lo << (8 - Bits)) | (lo >> (Bits - (8 - Bits)));
			(*entry)[0] =  lo8;
			(*entry)[1] = (lo8 * 2 + hi8) / 3;
			(*entry)[2] = (hi8 * 2 + lo8) / 3;
			(*entry)[3] =  hi8;
			entry++;
		}
	}
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
					uint32_t(*next)[4] = colorTable;
					for (uint32_t hi = 0; hi < (1 << Bits); hi++) {
						for (uint32_t lo = 0; lo < (1 << Bits); lo++) {
							uint32_t total_err = 0;

							for (uint32_t s = low_selector; s <= high_selector; s++) {
								int err = block_colors[s].g - (*next)[mapping[s]];

								total_err += err * err;
							}

							if (total_err < best_err) {
								best_err = total_err;
								best_lo = lo;
								best_hi = hi;
								//if (best_err == 0) {
								//	goto outer;
								//}
							}
							next++;
						}
					}
				//outer:
					assert(best_err <= 0xFFFF);
					*dst++ = (etc1_to_dxt1_56_solution){ (uint8_t)best_lo, (uint8_t)best_hi, (uint16_t)best_err };
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

					result[n] = (etc1_to_dxt1_56_solution){ (uint8_t)best_lo, (uint8_t)best_hi, (uint16_t)best_err };

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
	for (int n = 10; n > 0; n--) {
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
	bestRun(create_etc1_to_dxt1_6_conversion_table_original, "original");
	bestRun(create_etc1_to_dxt1_6_conversion_table<6>,       "optimised");
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
ADD_SIMD_TARGET
int main(int /*argc*/, char* /*argv*/[]) {
	ts_int32x4 op1, op2;
	int res; (void) res;
	
	op1 = ts_init_i32(1, 2, 3, 4);
	printInt32x4(op1);
	op2 = ts_mul_i32(op1, op1);
	printInt32x4(op2); // 1, 4, 9, 16
	
	op2 = ts_add_i32(op1, op2);
	printInt32x4(op2); // 2, 6, 12, 20
	
	op2 = ts_sub_i32(op2, op1);
	printInt32x4(op2); // 1, 4, 9, 16

	res = ts_hadd_i32(op2);
	printInt(res); // 30
	
	ALIGNED_VAR(int32_t, 16) mask[4] = {
		0xFFFFFFFF,
		0x00000000,
		0xFFFFFFFF,
		0x00000000,
	};
	op1 = ts_load_i32(mask);
	printInt32x4(op1);
	op2 = ts_and_u32(op1, op2);
	printInt32x4(op2);

	int32_t const shuffle[5] = {
		0x03020100, //  3,  2,  1,  0
		0x07060504, //  7,  6,  5,  4
		0x0B0A0908, // 11, 10,  9,  8
		0x0F0E0D0C, // 15, 14, 13, 12
		0xFFFFFFFF, // -1, -1, -1, -1 (fulfilling bit-7 on SSE and OOB on Neon)
	};
	
	op1 = ts_init_i32(0x33221100, 0x77665544, 0xBBAA9988, 0xFFEEDDCC);
	op2 = ts_init_i32(shuffle[3], shuffle[4], shuffle[1], shuffle[0]);
	printInt32x4(op1);
	op1 = ts_shuffle_u8(op1, op2);
	printInt32x4(op1);

	return 0;
}
