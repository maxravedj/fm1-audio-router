/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* When each MIDI message in (USB and the TRS jack: midi_in_q) came in, on plat_ticks24's clock (plat_midi_in_t24).
 *
 * The queue is filled by the 2 kHz poll (usb_poll, uart_midi_poll: TIMER5, sub-tick 0 and 5 of the 10 kHz ticks) and
 * emptied by the app's 1 kHz tick (plat_midi_in, sub-tick 0, right after the poll). A message that came in before the
 * poll at sub-tick 5 therefore waited up to 0.5 ms more in the queue, and the stamp taken when the app read it was late
 * by that, on top of the poll's own 0..0.5 ms: up to a millisecond after the message landed, and 1 ms apart from one
 * message to the next, which is a MIDI clock's jitter in the follower's tempo filter. This stamps each message in the
 * poll that took it, minus half a poll interval (it landed, on the average, half way through): within +-0.25 ms of
 * its arrival, as the 2 kHz poll alone gives (SLOOP / Optimist's 10 kHz peek of the endpoint is the 0.1 ms one).
 *
 * midi_stamp_poll() is called right after each poll, in the same interrupt, with the queue's write index (mi_w); a
 * message the app reads before it was stamped (plat_midi_in from elsewhere) is stamped with the read's time. */
#ifndef FM1B_MIDI_STAMP_H
#define FM1B_MIDI_STAMP_H

#define MIDI_POLL_HALF (250u * PLAT_TICKS_PER_US)   /* half the 2 kHz poll interval, in plat_ticks24's ticks */

static uint32_t midi_in_t24[MQ];                    /* the stamp of the message at the same index of midi_in_q */
static uint32_t mi_stamped;                         /* the stamps made so far (an index like mi_w) */

/* the messages enqueued since the last call came in during the poll that just ran: stamp them (now: ticks24 then) */
static void midi_stamp_poll(uint32_t now)
{
    while ((int32_t)(mi_w - mi_stamped) > 0)
        midi_in_t24[mi_stamped++ % MQ] = now - MIDI_POLL_HALF;
}

/* the stamp of the message at the read index mi_r (about to be taken); now when it has none yet */
static uint32_t midi_stamp_of_next(uint32_t now)
{
    return (int32_t)(mi_stamped - mi_r) > 0 ? midi_in_t24[mi_r % MQ] : now;
}

#endif
