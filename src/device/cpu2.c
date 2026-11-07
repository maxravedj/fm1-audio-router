/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The second core (fm1.toml cpu2 = true: FM1B_CPU2): plat_cpu2_* (plat.h) on felucca-bsp's chip/fm1_cpu1.h.
 * Part of device/unity.c. The start, the hold first thing at a start (main_fm1.c fm1_cstart) and the fault path
 * (fm1_irq.h with FM1_CPU1) are felucca-bsp's; here: when it may start, and the app's calls. */
#ifdef APP_UI_PROFILE
#define FM1_CPU1_IDLE_PAUSES() ui_profile_idle_spins
#endif
#include "fm1_cpu1.h"
#ifdef FM1B_RAM_RUNTIME
/* Only register accesses and the boot stack; callable before SRAM is copied. */
void __attribute__((section(".text.crt0.cpu2"),used,noinline)) fm1_ram_prestart(void)
{
    fm1_cpu1_stop();
}
#endif
#ifdef APP_CPU_WORKLOAD
#include "cpu2_workload.h"
static void (*cpu2_job_fn)(uint32_t);
static void cpu2_job(uint32_t arg)
{
    cpu2_work_begin();cpu2_job_fn(arg);cpu2_work_end();
}
#endif

static uint8_t cpu2_booting;                    /* app_init is running: the one time it may start (main_fm1.c) */

/* at boot, before app_init: the bar an earlier build set is lifted (FM1B_BUILD: tools/build.py build_id) */
static void cpu2_boot(void) { fm1_cpu1_bar_build(FM1B_BUILD); }

int plat_cpu2_set(int on)
{
    if (!on) {                                  /* (no job outstanding in the main loop: the render waits for its own) */
        fm1_cpu1_stop();
        fm1_cpu1_on = 0;
        return 0;
    }
    if (fm1_cpu1_on)
        return 0;
    /* at power-on only, in app_init, before the audio and the timers (the start that works: Melodee's, X0X's; safe
     * mode runs no app). With the fallback (fm1.toml cpu2_fallback), not after a failed start: one core; without,
     * always (a failure restarts, and two failed starts are safe mode) */
    if (!cpu2_booting || (FM1_CPU1_FALLBACK && bootguard.failed))
        return -1;
    return fm1_cpu1_start();
}
int plat_cpu2_on(void) { return fm1_cpu1_on; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg)
{
#ifdef APP_CPU_WORKLOAD
    if(fm1_cpu1_busy())return 0;
    cpu2_job_fn=fn;fm1_cpu1_sync();
    return fm1_cpu1_run(cpu2_job,arg);
#else
    return fm1_cpu1_run(fn,arg);
#endif
}
int plat_cpu2_wait(void) { return fm1_cpu1_wait(); }
void plat_cpu2_sync(void) { fm1_cpu1_sync(); }
int plat_cpu2_busy(void) { return fm1_cpu1_busy(); }
int plat_cpu2_gave_up(void) { return fm1_cpu1_gave_up; }
uint32_t plat_cpu2_bad(void) { return fm1_cpu1_mb.rejected; }
int plat_cpu2_barred(void) { return fm1_cpu1_barred(); }
uint32_t plat_cpu2_stack(void) { return fm1_cpu1_stack_used(); }
