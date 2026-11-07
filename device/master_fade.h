/* SPDX-License-Identifier: MPL-2.0
 * Speaker/headphone mute after the meter tap. Main loop publishes a target;
 * the audio producer owns the current gain and ramps continuously, even when
 * the target reverses mid-fade. Default 100 ms; configurable 0..1000 ms. */
#ifndef FM1B_MASTER_FADE_H
#define FM1B_MASTER_FADE_H
#include "plat.h"
#include "audio_gate.h"
static volatile uint32_t master_fade_target = 65536u;
static uint32_t master_fade_gain = 65536u;
static uint32_t master_fade_remainder;
static volatile uint32_t master_fade_frames=4410u;
static uint32_t master_fade_seen_frames=4410u;
void plat_master_mute_fade_ms(uint32_t ms)
{
    if(ms>1000u)ms=1000u;
    master_fade_frames=ms*441u/10u;
}
void plat_master_mute(int muted) { master_fade_target = muted ? 0u : 65536u; }
/* Boot only, before audio starts: restoring mute must never leak a fade-out. */
void plat_master_mute_init(int muted)
{
    master_fade_gain = muted ? 0u : 65536u;
    master_fade_target = master_fade_gain;
    master_fade_remainder = 0;
    master_fade_seen_frames=master_fade_frames;
}
static void master_fade_apply(int32_t *lr, uint32_t n)
{
    uint32_t i, target = master_fade_target, gain = master_fade_gain;
    uint32_t frames=master_fade_frames;
    if(frames!=master_fade_seen_frames){master_fade_remainder=0;master_fade_seen_frames=frames;}
    if (gain == 65536u && target == 65536u) return; /* preserve unmuted PCM exactly */
    uint32_t step=frames?65536u/frames:0,fraction=frames?65536u%frames:0;
    for (i = 0; i < n; i++) {
        float g;
        gain=audio_gate_tick(gain,target,&master_fade_remainder,frames,step,fraction);
        g = (float)gain * (1.0f / 65536.0f);
        lr[2u*i] = (int32_t)((float)lr[2u*i] * g);
        lr[2u*i+1u] = (int32_t)((float)lr[2u*i+1u] * g);
    }
    master_fade_gain = gain;
}
#endif
