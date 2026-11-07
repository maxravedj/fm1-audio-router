/* SPDX-License-Identifier: MPL-2.0
 * Audio taper: -60..0 dB over MASTER travel, exact silence at zero.
 * Q12 matches the existing mixer contract. Table generated as
 * round(4096 * 10^((-60 + 60*i/64)/20)); interpolation avoids pow in the ISR. */
#pragma once
#include <stdint.h>
static inline uint32_t fm1b_master_db(uint32_t position)
{
    static const uint16_t gain[65] = {
        4, 5, 5, 6, 6, 7, 8, 9, 10, 11, 12, 13, 15, 17, 19, 21, 23, 26, 29, 32, 35, 40, 44, 49, 55, 61, 68, 76, 84, 94, 104, 116, 130, 144, 161, 179, 199, 222, 248, 276, 307, 342, 381, 425, 473, 527, 587, 654, 728, 811, 904, 1007, 1122, 1249, 1392, 1551, 1727, 1924, 2143, 2388, 2660, 2963, 3301, 3677, 4096
    };
    uint32_t i, f;
    if (!position) return 0;
    if (position >= 4096u) return 4096u;
    i = position >> 6; f = position & 63u;
    return gain[i] + ((gain[i+1u] - gain[i]) * f + 32u) / 64u;
}
