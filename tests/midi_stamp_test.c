/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* MIDI-in stamps (device/midi_stamp.h): the TIMER5 schedule of main_fm1.c (10 kHz sub-ticks, the USB / TRS poll at
 * sub-ticks 0 and 5, the app's 1 kHz tick draining the queue at sub-tick 0 right after the poll) run over messages
 * landing at random times; each message's stamp against the time it landed. The stamp the app got before (its read's
 * time, plat_midi_in_t24 = fm1_ticks()) is measured beside it. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define PLAT_TICKS_PER_US 24u
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "usb.c"
#include "midi_stamp.h"

#define TICK (100u * PLAT_TICKS_PER_US)           /* one 10 kHz sub-tick */
static int fails;
static void check(int ok, const char *what)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

int main(void)
{
    enum { N = 20000 };
    static uint32_t landed[N + 8];
    uint32_t t = 100000u, k, sub = 0, sent = 0, got = 0, next = 0;
    double sum_new = 0, sum_old = 0, max_new = 0, max_old = 0, worst_wait = 0;
    uint32_t lcg = 12345u;
    /* messages at random times, 0.05 .. 6 ms apart (a clock's 20 ms is the same: only the phase matters) */
    next = t + 3u * TICK;
    while (got < N) {
        uint32_t now = t;
        /* the messages that landed before this sub-tick's start, queued by the poll that follows */
        while (sent < N && next <= now) {
            landed[sent++] = next;
            lcg = lcg * 1664525u + 1013904223u;
            next += 1200u + (lcg >> 8) % (140u * PLAT_TICKS_PER_US * 40u);
            /* (a message is queued by the poll: held here until it runs) */
        }
        if (sub % 5u == 0u) {                      /* the poll: everything landed so far is queued */
            while (mi_w < sent)
                (void)midi_enqueue(0x9009u | mi_w << 24, 1);
            midi_stamp_poll(now);
        }
        if (sub == 0u) {                           /* the app's tick: plat_midi_in drains the queue */
            while (mi_r != mi_w) {
                uint32_t at = landed[got], st = midi_stamp_of_next(now), old = now;
                double en = (double)(int32_t)(st - at) / PLAT_TICKS_PER_US;
                double eo = (double)(int32_t)(old - at) / PLAT_TICKS_PER_US;
                sum_new += en < 0 ? -en : en;
                sum_old += eo < 0 ? -eo : eo;
                if ((en < 0 ? -en : en) > max_new)
                    max_new = en < 0 ? -en : en;
                if ((eo < 0 ? -eo : eo) > max_old)
                    max_old = eo < 0 ? -eo : eo;
                if (eo > worst_wait)
                    worst_wait = eo;
                mi_r++;
                got++;
            }
        }
        sub = (sub + 1u) % 10u;
        t += TICK;
    }
    printf("  %u messages: stamp error against the landing time (us): mean %.0f, worst %.0f (before: read time, mean %.0f, "
           "worst %.0f)\n", got, sum_new / got, max_new, sum_old / got, max_old);
    check(max_new <= 250.5, "every stamp within +-0.25 ms of the message's landing");
    check(max_old > 900.0, "(the read-time stamp was up to a millisecond late: the model sees it)");
    check(sum_new < sum_old / 2, "the mean error is less than half of the read time's");
    {   /* a message read before any poll stamped it: the read's time, and nothing goes out of step */
        uint32_t w0 = mi_w;
        (void)midi_enqueue(0x9009u, 1);
        check(mi_w == w0 + 1u && midi_stamp_of_next(777u) == 777u, "an unstamped message takes the read's time");
        midi_stamp_poll(100000u);
        check(midi_stamp_of_next(777u) == 100000u - MIDI_POLL_HALF, "a stamp made by the poll is kept");
        mi_r = mi_w;
    }
    printf(fails ? "midi_stamp_test: %d FAILED\n" : "midi_stamp_test: all passed\n", fails);
    return fails != 0;
}
