/* SPDX-License-Identifier: MPL-2.0
 * Focused research harness: reuse the BSP's fake USB host/controller and
 * sine-fit measurement, exercising its real playback code in both modes.
 * These are digital model results, not FM-1 analog output measurements. */
#define main bsp_uac_full_suite_main
#include "../bsp/tests/uac_test.c"
#undef main

int main(void)
{
    static const uint32_t frequencies[] = {1000u, 10000u, 16000u};
    uint32_t i;
    for (i = 0; i < sizeof frequencies / sizeof frequencies[0]; i++) {
        play_case c = {0, 0, 1, frequencies[i], 1};
        double residual = test_play_clock(c);
        if (TEST_FB) assert(residual < -88.0);
    }
    puts("PASS: quality comparison completed without playback under/overruns (simulated USB host)");
    return 0;
}
