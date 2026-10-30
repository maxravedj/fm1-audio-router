/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The switchable features' blocks Optimist's arena lends (its core/arena_bufs.c ARB_LEND0 + i, core/feat_mem.c), in
 * this order, those built (included by core/opt_core.c before engines/engines.c, which counts them: FMEM_NLEND):
 *   L_BTA  Bluetooth audio's queue and ring (fm1.toml bt_audio_mem = "app": plat.h plat_bta_mem; ui/opt_bta_lend.h),
 *          17.7 KB while an AUDIO mode is on or its run is let go
 *   L_MIR  the mirror's Bluetooth ring (io/editor/ed_mirror.c mbr_q, MIR_LEND), 8 KB while a Bluetooth client uses it
 * Each its size, wanted now, and lent or taken back (the interrupts off): ui/opt_feat.h of_lend_*, after the code they
 * read. Each is a feature of the questions' table too (core/opt_feat_decl.h FMEM_FEATS: its name, its size) */
#pragma once
#if defined(FM1B_BTA_MEM) && FM1B_BTA_MEM && FELUCCA_BT_AUDIO
#define OPT_LEND_BTA 1
#else
#define OPT_LEND_BTA 0
#endif
#if defined(FELUCCA_ARENA_BYTES) && FELUCCA_FX_ARENA && FELUCCA_EDITOR && FELUCCA_ED_MIRROR && FELUCCA_BLE
#define MIR_LEND 1                               /* (ed_mirror.c: the ring a pointer, lent) */
#else
#define MIR_LEND 0
#endif
#define FMEM_NLEND (OPT_LEND_BTA + MIR_LEND)
#define L_BTA 0
#define L_MIR OPT_LEND_BTA
#if FMEM_NLEND
static uint32_t of_lend_bytes(uint32_t i);
static int of_lend_want(uint32_t i);
static void of_lend_set(uint32_t i, void *p);
#define FMEM_LEND_BYTES(i) of_lend_bytes(i)
#define FMEM_LEND_WANT(i) of_lend_want(i)
#define FMEM_LEND_SET(i, p) of_lend_set(i, p)
#endif
