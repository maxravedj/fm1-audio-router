/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The switchable features' table as Optimist's core/feat_mem.c reads it (included by core/opt_core.c before fx/fx.c,
 * which includes core/arena_bufs.c and feat_mem.c): the features and their hooks are declared here and defined after
 * the UI (ui/opt_feat.h), in the one unit. Only Bluetooth audio's buffers are lendable yet (fm1.toml bt_audio_mem =
 * "app": FELUCCA_BTA_LEND); the other features are in the table with their idle signals (one place: SYSTEM > INFO
 * may show what an idle one would free once its buffers are lendable too) and never offered (bytes 0) */
#pragma once
#if FELUCCA_BTA_LEND
enum { OF_BTA, OF_BLE, OF_UAC, OF_WIFI, OF_AP, OF_EDITOR, OF_USBH, OF_N };
static uint32_t of_bytes(uint32_t f);
static int of_on(uint32_t f);
static int of_idle(uint32_t f);
static void of_off(uint32_t f);
static uint32_t bta_lend_bytes(void);
#define FMEM_BYTES(f) of_bytes(f)
#define FMEM_ON(f) of_on(f)
#define FMEM_IDLE(f) of_idle(f)
#define FMEM_OFF(f) of_off(f)
#define FMEM_FEATS                                                                                                      \
    {"BT AUDIO", "SYSTEM > BT", ARB_K(ARB_BTA)}, {"BT MIDI", "SYSTEM > BT", FMEM_NO_K},                              \
    {"USB AUDIO", "SYSTEM > USB", FMEM_NO_K}, {"WI-FI", "SYSTEM > WI-FI", FMEM_NO_K},                                \
    {"WI-FI AP", "SYSTEM > WI-FI", FMEM_NO_K}, {"EDITOR", "SYSTEM > USB", FMEM_NO_K}, {"USB HOST", "SYSTEM > USB", FMEM_NO_K}
/* the pause of a YES to a feature's PACK, and the UNLOAD of a NOFIT (ui/opt_feat.h) */
static int of_playing(void);
static void of_pause(void);
static void of_resume(void);
static int of_quiet(void);
static void of_unload(void);
static void of_say_full(uint32_t f);
#define FMEM_PLAYING() of_playing()
#define FMEM_PAUSE() of_pause()
#define FMEM_RESUME() of_resume()
#define FMEM_QUIET() of_quiet()
#define FMEM_UNLOAD() of_unload()
#define FMEM_SAY_FULL(f) of_say_full(f)
#endif
