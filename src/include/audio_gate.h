/* SPDX-License-Identifier: MPL-2.0
 * Bounded Q16 linear mute ramps. No division in the per-frame step. */
#ifndef FM1B_AUDIO_GATE_H
#define FM1B_AUDIO_GATE_H
#include <stdint.h>
static inline uint32_t audio_gate_tick(uint32_t gain,uint32_t target,uint32_t *rem,
        uint32_t frames,uint32_t step,uint32_t fraction)
{
    if(gain==target)return gain;
    if(!frames){*rem=0;return target;}
    *rem+=fraction;
    if(*rem>=frames){*rem-=frames;step++;}
    if(gain<target)gain=target-gain>step?gain+step:target;
    else gain=gain-target>step?gain-step:target;
    if(gain==target)*rem=0;
    return gain;
}
static inline uint32_t audio_gate_skip(uint32_t gain,uint32_t target,uint32_t *rem,
        uint32_t frames,uint32_t step,uint32_t fraction,uint32_t count)
{
    if(gain==target || !count)return gain;
    if(!frames || count>=frames){*rem=0;return target;}
    /* frames <= 44100: both products and the remainder fit uint32_t. */
    uint32_t ticks=*rem+count*fraction;
    uint32_t amount=count*step+ticks/frames;*rem=ticks%frames;
    if(gain<target)gain=target-gain>amount?gain+amount:target;
    else gain=gain-target>amount?gain-amount:target;
    if(gain==target)*rem=0;
    return gain;
}
#endif
