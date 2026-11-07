/* SPDX-License-Identifier: MPL-2.0
 * Real BSP mixer, including the production post-MASTER observer hook. */
#include "plat.h"
uint32_t plat_lock(void) { return 0; }
void plat_unlock(uint32_t lock) { (void)lock; }
#define APP_MASTER_METER
#define main original_mix_test_main
#include "../../../tests/uac_mix_test.c"
#undef main
#include <assert.h>
int main(void)
{
    unsigned full, m;
    uint32_t peak[2];
    assert(!original_mix_test_main());
    for (full = 0; full < 2; full++) for (m = 0; m < 3; m++) {
        const uint32_t positions[] = {0,2048,4096};
        int32_t l, r, rec;
        plat_master_peaks(peak);
        block(positions[m]*positions[m]>>12, positions[m], full, 1024, &l, &r, &rec);
        plat_master_peaks(peak);
        assert(peak[0] == (l < 0 ? -(uint32_t)l : (uint32_t)l));
        assert(peak[1] == (r < 0 ? -(uint32_t)r : (uint32_t)r));
        plat_master_peaks(peak); assert(!peak[0] && !peak[1]);
    }
    {
        const int32_t extremes[] = {-2147483647-1,2147483647};
        master_meter_samples(extremes,1); plat_master_peaks(peak);
        assert(peak[0] == 8388608 && peak[1] == 8388608);
    }
    puts("PASS: actual BSP mix observer matches final stereo PCM after MASTER/trim, full-recording mode, clear and clamp");
}
