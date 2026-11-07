/* SPDX-License-Identifier: MPL-2.0
 * Read-only post-mix stereo peaks. Audio render is the sole producer; the
 * main loop snapshots two words under a short interrupt lock. No PCM writes. */
#ifndef FM1B_MASTER_METER_H
#define FM1B_MASTER_METER_H
#include "plat.h"
static volatile uint32_t master_meter_peak[2];
static volatile uint8_t master_meter_enabled = 1;
void plat_master_meter_enable(int enable)
{
    uint32_t lock = plat_lock();
    master_meter_enabled = enable != 0;
    master_meter_peak[0] = master_meter_peak[1] = 0;
    plat_unlock(lock);
}
static void master_meter_samples(const int32_t *lr, uint32_t n)
{
    uint32_t i, l = 0, r = 0;
#ifdef APP_AUDIO_MONITOR
    audio_monitor_master_samples(lr,n);
#endif
    if (!master_meter_enabled) return;
    for (i = 0; i < n; i++) {
        uint32_t a = lr[2u*i] < 0 ? 0u - (uint32_t)lr[2u*i] : (uint32_t)lr[2u*i];
        uint32_t b = lr[2u*i+1u] < 0 ? 0u - (uint32_t)lr[2u*i+1u] : (uint32_t)lr[2u*i+1u];
        if (a > l) l = a;
        if (b > r) r = b;
    }
    if (l > 8388608u) l = 8388608u;
    if (r > 8388608u) r = 8388608u;
    if (l > master_meter_peak[0]) master_meter_peak[0] = l;
    if (r > master_meter_peak[1]) master_meter_peak[1] = r;
}
void plat_master_peaks(uint32_t lr[2])
{
    uint32_t lock = plat_lock();
    lr[0] = master_meter_peak[0]; lr[1] = master_meter_peak[1];
    master_meter_peak[0] = master_meter_peak[1] = 0;
    plat_unlock(lock);
}
#endif
