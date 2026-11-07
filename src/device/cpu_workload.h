/* SPDX-License-Identifier: MPL-2.0
 * Wall time spent in measured work, with nested IRQs counted once.
 * Timer/USB scan, audio and foreground UI share this meter. Tight idle polling
 * is excluded, as are uninstrumented platform housekeeping and ISR entry/exit.
 * It includes LCD bus waits: useful workload/headroom, not instruction retirement. */
#pragma once
#include "plat.h"
#include "ui_profile.h"
static volatile uint32_t cpu_work_depth, cpu_work_start, cpu_work_ticks;
static uint32_t cpu_work_window, cpu_work_pct;
#ifdef APP_UI_PROFILE
uint32_t plat_profile_work_pending(void){return cpu_work_depth?plat_ticks24()-cpu_work_start:0;}
#endif
void plat_cpu_work_reset(void)
{
    uint32_t f = plat_lock();
    cpu_work_depth = cpu_work_ticks = cpu_work_pct = 0;
    cpu_work_window = plat_ticks24();
    plat_unlock(f);
}
void plat_cpu_work_begin(void)
{
    uint32_t f = plat_lock();
    if (cpu_work_depth++ == 0u) cpu_work_start = plat_ticks24();
    plat_unlock(f);
}
void plat_cpu_work_end(void)
{
    uint32_t f = plat_lock();
    if (cpu_work_depth && --cpu_work_depth == 0u) {
        uint32_t ticks=plat_ticks24()-cpu_work_start;
        cpu_work_ticks += ticks;plat_profile_add(PROFILE_WORK,ticks);
    }
    plat_unlock(f);
}
uint32_t plat_cpu_work_pct(void)
{
    uint32_t f = plat_lock(), now = plat_ticks24(), dt = now - cpu_work_window;
    if (dt >= 250u * 1000u * PLAT_TICKS_PER_US) {
        uint32_t busy = cpu_work_ticks;
        if (cpu_work_depth) {
            uint32_t ticks=now-cpu_work_start;
            busy += ticks;plat_profile_add(PROFILE_WORK,ticks);
            cpu_work_start = now;
        }
        /* Divide first to avoid a 32-bit overflow (or a 64-bit divide). */
        cpu_work_pct = busy >= dt ? 100u : busy / (dt / 100u);
        cpu_work_ticks = 0;
        cpu_work_window = now;
    }
    plat_unlock(f);
    return cpu_work_pct;
}
