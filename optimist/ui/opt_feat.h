/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The switchable features' hooks (core/opt_feat_decl.h declares them; Optimist's core/feat_mem.c reads them), after the
 * UI: what each feature's setting is, whether it is idle, and its switch-off as the user's own (kept in the settings
 * record). The idle signals in one place:
 *   BT AUDIO   an AUDIO mode on, no device paging, linked, setting up, streaming or being scanned for
 *   BT MIDI    on, no computer connected, no device connected (the central)
 *   USB AUDIO  no computer has chosen an audio alt setting (no stream either way; USB MIDI may be in use)
 *   WI-FI      a mode on, not joined, or joined with no Link peer and no editor session
 *   WI-FI AP   running, no client
 *   EDITOR     no editor or Mirror request for EDITOR_IDLE_MS
 *   USB HOST   on, no device enumerated
 *   MIRROR     never idle while on (a client mirrors over Bluetooth, or asked something in the last 3 s)
 * Only the blocks of core/opt_lend.h are lendable (of_bytes: BT AUDIO's buffers, the mirror's Bluetooth ring): the
 * others are never offered. The pause of a YES to PACK: the
 * transport stopped (what sounds is faded out by feat_mem.c), the parts quiet before the pack (fx_arena.c part_quiet); UNLOAD: the PROJECT page's SAVE into the open project once stopped (op_edits.c
 * prj_save_go), then a NEW project; none open yet: nothing saved, said (SAVE FIRST) and the setting back off */
#pragma once
#if FMEM_NLEND
#define EDITOR_IDLE_MS 60000u
static uint32_t of_bytes(uint32_t f)
{
#if OPT_LEND_BTA
    if (f == OF_BTA)
        return plat_bta_mem_bytes();
#endif
#if MIR_LEND
    if (f == OF_MIR)
        return MIR_BLE_RING;
#endif
    (void)f;
    return 0u;
}
/* core/opt_lend.h's blocks by their index there: the size, wanted now, lent or taken back (core/arena_bufs.c) */
static const uint8_t OF_LENT[] = {
#if OPT_LEND_BTA
    OF_BTA,
#endif
#if MIR_LEND
    OF_MIR,
#endif
};
static uint32_t of_lend_bytes(uint32_t i) { return of_bytes(OF_LENT[i]); }
static int of_lend_want(uint32_t i)
{
#if OPT_LEND_BTA
    if (i == L_BTA)
        return bta_lend_want();
#endif
#if MIR_LEND
    if (i == L_MIR)
        return mir_lend_want();
#endif
    return 0;
}
static void of_lend_set(uint32_t i, void *p)    /* (the interrupts off: arb_set) */
{
#if OPT_LEND_BTA
    if (i == L_BTA)
        bta_lend(p);
#endif
#if MIR_LEND
    if (i == L_MIR)
        mir_lend(p);
#endif
}
#if OPT_LEND_BTA
static int of_bta_busy(void)
{
    uint32_t s = bta_state();
    return s == BTA_S_SCANNING || s == BTA_S_PAGING || s == BTA_S_LINKED || s == BTA_S_SETUP || s == BTA_S_STREAMING;
}
#endif
static int of_editor_idle(void)
{
#if FELUCCA_EDITOR && FELUCCA_ED_MIRROR
    return !mir.on && fm1_ms - mir.last_ms > EDITOR_IDLE_MS;
#else
    return 0;                                    /* (no signal: never idle) */
#endif
}
static int of_on(uint32_t f)
{
    switch (f) {
#if OPT_LEND_BTA
    case OF_BTA:
        return bta.mode != BTA_OFF;
#endif
#if MIR_LEND
    case OF_MIR:
        return mir_lend_on();
#endif
#if FELUCCA_BLE
    case OF_BLE:
        return ble_on != 0;
#endif
#if FELUCCA_USB_AUDIO
    case OF_UAC:
        return 1;                                /* (no setting yet: always on) */
#endif
#if FELUCCA_WIFI
    case OF_WIFI:
        return wn_st.mode != WN_MODE_OFF && wn_st.mode != WN_MODE_AP && wn_st.mode != WN_MODE_AP_MAN;
#endif
#if FELUCCA_WIFI_AP
    case OF_AP:
        return wn_st.mode == WN_MODE_AP || wn_st.mode == WN_MODE_AP_MAN;
#endif
#if FELUCCA_EDITOR
    case OF_EDITOR:
        return 1;
#endif
#if FELUCCA_USB_HOST
    case OF_USBH:
        return plat_usb_host_state() != USBH_OFF;
#endif
    default:
        return 0;
    }
}
static int of_idle(uint32_t f)
{
    if (!of_on(f))
        return 0;
    switch (f) {
#if OPT_LEND_BTA
    case OF_BTA:
        return !of_bta_busy();
#endif
#if FELUCCA_BLE
    case OF_BLE:
        return !plat_ble_connected() && plat_ble_device() != BLE_DEV_READY;
#endif
#if FELUCCA_USB_AUDIO
    case OF_UAC: {
        plat_usb_audio_stats_t s;
        plat_usb_audio_stats(&s);
        return !s.in_alt && !s.out_alt;
    }
#endif
#if FELUCCA_WIFI
    case OF_WIFI:
        return plat_wifi_step() != WIFI_STEP_UP || (plat_link_peers() == 0u && of_editor_idle());
#endif
#if FELUCCA_WIFI_AP
    case OF_AP:
        return plat_wifi_ap_clients() == 0u;
#endif
    case OF_EDITOR:
        return of_editor_idle();
#if FELUCCA_USB_HOST
    case OF_USBH:
        return plat_usb_host_state() != USBH_CONNECTED;
#endif
    default:
        return 0;
    }
}
static void of_off(uint32_t f)                   /* (only the lendable ones are ever switched off here) */
{
#if MIR_LEND
    if (f == OF_MIR) {
        mir_lend_off();                          /* (the Bluetooth mirror ends; asked again once the central has gone) */
        return;
    }
#endif
#if OPT_LEND_BTA
    if (f != OF_BTA || !of_on(f))
        return;
    bt_enter(BT_AUD, BT_OFF);                    /* (as MODE OFF on SYSTEM > BT: its block goes back by itself) */
    sys_dirty = 1;
    settings_later = 1;                          /* (kept: the settings record, once quiet) */
#endif
    (void)f;
}
static uint8_t of_unload_st;                     /* UNLOAD asked for feature n - 1: waiting for the transport to stop */
static int of_playing(void) { return song.playing || transport_req == 1u; }
static void of_pause(void)                       /* (the voices: feat_mem.c fmem_hush, after) */
{
    if (song.playing || transport_req == 1u)
        transport_req = 2;                       /* (stopped: the sequencer's notes end) */
}
static void of_resume(void) { transport_req = 1; }
static int of_quiet(void)
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        if (!part_quiet(k))
            return 0;
    return 1;
}
static void of_unload(uint32_t f)
{
    if (prj_cur < 0) {
        ui_message("SAVE FIRST");                /* (no open project to save into: nothing unloaded) */
        of_off(f);
        return;
    }
    if (song.playing || transport_req == 1u)
        transport_req = 2;
    of_unload_st = (uint8_t)(f + 1u);
}
static void of_say_full(uint32_t f)
{
    char b[24];
    str_cpy(b, "MEM FULL ", sizeof b);
    str_app(b, FMEM[f].name, sizeof b);
    ui_message(b);
}
/* every main loop pass (opt_bt_poll): an UNLOAD once stopped: saved, then a NEW project; the block placed by itself
 * (arb_poll) once the new project's parts have switched */
static void of_poll(void)
{
    uint32_t f = of_unload_st - 1u;
    if (!of_unload_st || song.playing || transport_req)
        return;
    of_unload_st = 0;
    if (prj_save_go((uint32_t)prj_cur)) {
        of_off(f);                               /* (not saved: nothing unloaded, the setting back off) */
        return;
    }
    op_project_new();
    ui_message("SAVED, NEW PROJECT");
}
#else
static void of_poll(void) {}
#endif
