/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <stdint.h>
/* Exact 96-tap stereo convolution. High words plus unsigned low bytes encode
 * signed 24-bit PCM. Interpolate between adjacent Q15 coefficient rows.
 * The high-word ring must be aligned to four bytes (packed stereo loads). */
void btr_fir24(const int16_t high[2048][2],const uint8_t low[2048][2],
               uint32_t at,const int16_t coeff[192],uint32_t frac,int32_t out[2]);
