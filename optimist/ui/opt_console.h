/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's USB serial console in the trial (USB_CONSOLE: FELUCCA_CDC; SYSTEM > USB SERIAL, op_project.c) on fm1-base's
 * (fm1.toml usb_console, from the configuration: gen_optimist.py --conf; plat.h plat_console_*). The setting as
 * Optimist keeps it (usb.c's usb_serial: the settings word's bit 16, storage/settings_word.c), presented from the next
 * start (usb_cdc_on: opt_con_boot, before fm1-base starts USB). The command line is Optimist's (io/console.c cdc_task:
 * the prompt, the line editing, "help", "? (help)"), with the commands that mean something on fm1-base's platform:
 *   play | pause | stop | rec on|off | proj load N | heavy [N]   Optimist's run control (io/console_run.c, as it is)
 *   status       the version, the time, the load, the guard, the late halves, the voices, the second core
 *   params       the selected track's parameters (console.c con_params)
 *   btlog [N]    fm1-base's Bluetooth log, the newest N lines (plat_ble_log_line: its stack's own lines, what
 *                Optimist's blell / btinq / bttrace print from its own stack on its console; BLE or BT_AUDIO builds)
 *   reboot       a restart (plat_restart)
 * The rest of Optimist's console reads its own HAL (registers, the second core's setup, its radio, SD bus and flash
 * driver), which fm1-base keeps to itself: not here. Main loop only (ui/opt_ui.c opt_ui_frame). */
#pragma once

#if FELUCCA_CDC
#ifndef FELUCCA_CDC_FORCE
#define FELUCCA_CDC_FORCE 0                     /* (usb.c's: 1, the console whatever USB SERIAL says) */
#endif
#define CON_LINE 64u
#include "io/console_parse.h"                   /* con_word, con_num, con_run_parse (Optimist's, pure) */
static struct {
    char line[CON_LINE];
    uint32_t len;
    uint8_t dtr_seen, stalled;                  /* stalled: the computer stopped reading, the rest of the reply dropped */
} con;

static void con_putc(char c)                    /* (console.c's: a full ring waits for the computer, briefly) */
{
    uint32_t t0 = plat_ms();
    while (!con.stalled && !plat_console_write(&c, 1)) {
        if (!plat_console_open() || plat_ms() - t0 > 20u) {
            con.stalled = 1;
            return;
        }
        plat_yield();
    }
}
static void con_puts(const char *s)
{
    while (*s)
        con_putc(*s++);
}
static void con_dec(int32_t v)
{
    char b[12];
    uint32_t n = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0)
        con_putc('-');
    do
        b[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) != 0 && n < sizeof b);
    while (n)
        con_putc(b[--n]);
}
static void con_kv(const char *k, int32_t v)    /* "key value\r\n" */
{
    con_puts(k);
    con_putc(' ');
    con_dec(v);
    con_puts("\r\n");
}
#include "io/console_run.c"                     /* play, pause, stop, rec, proj load, heavy (Optimist's run control) */

static void con_status(void)
{
    con_puts("optimist ");
    con_puts(FELUCCA_VERSION);
    con_puts(" on fm1-base\r\n");
    con_kv("uptime_ms", (int32_t)plat_ms());
    con_kv("cpu_pct", (int32_t)(song.cpu_q8 * 100u / 256u));
    con_kv("cpu_khz", (int32_t)cpu_khz);
#if FELUCCA_CPU_GUARD
    con_kv("cpu_guard", (int32_t)cg.level);
    con_kv("cpu_guard_steps", (int32_t)cg.steps);
    con_kv("cpu_guard_shed", (int32_t)cg.sheds);
#endif
    con_kv("audio_late", (int32_t)ovl.drops);
    con_kv("voices", (int32_t)opt_core_voices());
    con_kv("playing", song.playing);
#if FELUCCA_DUAL >= 2
    con_kv("cpu1_up", opt_core_dual_stat(0));
    con_kv("cpu1_why", opt_core_dual_stat(1));
    con_kv("cpu1_jobs", opt_core_dual_stat(2));
    con_kv("cpu1_posts", opt_core_dual_stat(3));
#endif
}
static void con_params(void)                    /* (console.c's) */
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++) {
        con_dec((int32_t)i);
        con_putc('=');
        con_dec(TSEL->p[i]);
        con_puts(i % 8u == 7u || i + 1u == P_COUNT ? "\r\n" : " ");
    }
}
#if FELUCCA_BLE || FELUCCA_BT_AUDIO
static void con_btlog(const char *p)            /* the newest N lines (default 16), the oldest first */
{
    int ok, back;
    uint32_t n = con_num(&p, &ok), total = plat_ble_log_total();
    if (!ok || !n)
        n = 16u;
    con_kv("lines", (int32_t)total);
    for (back = (int)(n < total ? n : total) - 1; back >= 0; back--) {
        const char *l = plat_ble_log_line(back);
        if (l) {
            con_puts(l);
            con_puts("\r\n");
        }
    }
}
#endif

static void con_exec(const char *p)
{
    if (con_run(p))
        return;
    if (con_word(&p, "help") || con_word(&p, "?"))
        con_puts("play  pause  stop  rec on|off  proj load N  heavy [NOTES]  status  params"
#if FELUCCA_BLE || FELUCCA_BT_AUDIO
                 "  btlog [N]"
#endif
                 "  reboot\r\n");
    else if (con_word(&p, "status"))
        con_status();
    else if (con_word(&p, "params"))
        con_params();
#if FELUCCA_BLE || FELUCCA_BT_AUDIO
    else if (con_word(&p, "btlog"))
        con_btlog(p);
#endif
    else if (con_word(&p, "reboot")) {
        con_puts("rebooting\r\n");
        plat_restart();
    } else if (*p)
        con_puts("? (help)\r\n");
}

/* every main loop pass: the prompt when a computer opens the port, the line typed (console.c cdc_task) */
static void opt_con_task(void)
{
    int c;
    if (plat_console_open() && !con.dtr_seen) {
        con.stalled = 0;
        con_puts("\r\nOptimist ");
        con_puts(FELUCCA_VERSION);
        con_puts(" console (fm1-base) - 'help'\r\n> ");
    }
    con.dtr_seen = (uint8_t)plat_console_open();
    while ((c = plat_console_read()) >= 0) {
        if (c == '\r' || c == '\n') {
            if (c == '\n' && con.len == 0)
                continue;                       /* (the LF of a CR LF) */
            con.stalled = 0;
            con_puts("\r\n");
            con.line[con.len] = 0;
            con_exec(con.line);
            con.len = 0;
            con_puts("> ");
        } else if (c == 0x7F || c == 0x08) {
            if (con.len) {
                con.len--;
                con_puts("\b \b");
            }
        } else if (c >= ' ' && c < 0x7F && con.len + 1u < CON_LINE) {
            con.line[con.len++] = (char)c;
            con_putc((char)c);
        }
    }
}
/* at power-on, after the settings (USB SERIAL read): the console presented or not from this start, before fm1-base
 * starts USB (app_init: no re-enumeration), as usb.c's usb_start takes usb_cdc_on from usb_serial */
static void opt_con_boot(void)
{
    usb_cdc_on = (uint8_t)(usb_serial || FELUCCA_CDC_FORCE);
    plat_console_present(usb_cdc_on);
}
#else
static void opt_con_task(void) {}
static void opt_con_boot(void) {}
#endif
