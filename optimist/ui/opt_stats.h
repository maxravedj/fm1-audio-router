/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's part of fm1-base's debug stats (plat.h APP_STATS: device/dbgstats.c keeps them in flash, a snapshot every
 * 5 minutes while SYSTEM > DEBUG > DEBUG STATS is on, Optimist's core/dbgstats.h): its 13 bytes of each record, in
 * the order app.h's APP_STATS_LAYOUT names them, and when a write disturbs nothing. Main loop, once a second: reads
 * only (the overload monitor's rings, the stacks' marks, the CPU guard's counts), no lock */
static uint8_t opt_st_u8(uint32_t v) { return (uint8_t)(v < 255u ? v : 255u); }
static void opt_st_u16(uint8_t *p, uint32_t v)
{
    v = v < 65535u ? v : 65535u;
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8);
}
void app_stats(uint8_t out[PLAT_STATS_APP])
{
    uint32_t ms = plat_ms(), which;
    int32_t left = stk_least(&which);
    _Static_assert(PLAT_STATS_APP == 13u, "app.h APP_STATS_LAYOUT: 13 bytes");
    out[0] = opt_st_u8(ovl_win_pct(OVL_CPU0, ms, OVL_W10));   /* c10, c60, call: SYSTEM > INFO's CPU0 MAX 10S, 1M, MAX */
    out[1] = opt_st_u8(ovl_win_pct(OVL_CPU0, ms, OVL_W60));
    out[2] = opt_st_u8(ovl_maxall_pct(OVL_CPU0));
#if FELUCCA_CPU_GUARD
    out[3] = opt_st_u8(cg.steps);                    /* guard: the CPU guard's steps up (system/cpuguard.h) */
#endif
    out[4] = left < 0 ? 255u : (uint8_t)which;       /* stk: STK_MAIN, STK_IRQ, STK_C1 (system/stackmark.h) */
    opt_st_u16(out + 5, ovl.drops);                  /* drops: late halves since the start (INFO's DROPS before a clear) */
    opt_st_u16(out + 7, ovl_midi_lost());            /* midi: MIDI in lost, its ring full */
    opt_st_u16(out + 9, left < 0 ? 65535u : (uint32_t)left);   /* left: that stack's bytes never used */
    opt_st_u16(out + 11, stk_sram_free());           /* sram: INFO's RAM .. FREE */
}
int app_stats_quiet(void) { return !(song.playing || transport_req); }   /* (the transport stopped: SAVE_WAITS_PLAY's) */
