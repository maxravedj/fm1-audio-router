/* SPDX-License-Identifier: MPL-2.0 */
/* Frozen 0.0.38 producer proves every retained bin, including partial groups. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "plat.h"
uint32_t plat_ms(void){return 0;}
uint32_t plat_lock(void){return 0;}
void plat_unlock(uint32_t n){(void)n;}
#include "../device/usb_input_waveform.h"
typedef struct {int16_t v[4];} frozen_bin_t;
static volatile frozen_bin_t frozen_history[USB_WAVE_LEVELS][USB_WAVE_RING];
static frozen_bin_t frozen_accum[USB_WAVE_LEVELS];
static uint32_t frozen_count,frozen_filled;
static void old_push(int32_t l, int32_t r)
{
    uint32_t level, k, count = frozen_count + 1u;
    frozen_bin_t bin = {{(int16_t)(l >> 8), (int16_t)(l >> 8),
                           (int16_t)(r >> 8), (int16_t)(r >> 8)}};
    for (k = 0; k < 4u; k++) frozen_history[0][(count - 1u) & (USB_WAVE_RING - 1u)].v[k] = bin.v[k];
    for (level = 1; level < USB_WAVE_LEVELS; level++) {
        uint32_t shift = 2u * level, child_shift = shift - 2u;
        frozen_bin_t *a = &frozen_accum[level];
        if ((((count - 1u) >> child_shift) & 3u) == 0) *a = bin;
        else for (k = 0; k < 4u; k += 2u) {
            if (bin.v[k] < a->v[k]) a->v[k] = bin.v[k];
            if (bin.v[k+1u] > a->v[k+1u]) a->v[k+1u] = bin.v[k+1u];
        }
        if (count & ((1u << shift) - 1u)) break;
        bin = *a;
        for (k = 0; k < 4u; k++)
            frozen_history[level][((count >> shift) - 1u) & (USB_WAVE_RING - 1u)].v[k] = bin.v[k];
    }
    if (frozen_filled < USB_WAVE_RING * 4096u) frozen_filled++;
    frozen_count = count;
}

static void identical(void)
{
    assert(sizeof usb_wave_history==sizeof frozen_history);
    assert(!memcmp((void *)usb_wave_history,(void *)frozen_history,sizeof frozen_history));
    assert(!memcmp(usb_wave_accum,frozen_accum,sizeof frozen_accum));
    assert(usb_wave_count==frozen_count && usb_wave_filled==frozen_filled);
}
int main(void)
{
    uint32_t random=61;assert(sizeof(usb_wave_bin_t)==8 && __alignof__(usb_wave_bin_t)>=4);
    for(unsigned run=0;run<3;run++) {
        usb_wave_count=frozen_count=run?UINT32_MAX-257u:0;
        usb_wave_filled=frozen_filled=run==2?USB_WAVE_RING*4096u-12u:0;
        for(unsigned i=0;i<400000;i++) {
            random=random*1664525u+1013904223u;int32_t l=(int16_t)random;
            random=random*1664525u+1013904223u;int32_t r=(int16_t)random;
            if(i%31==0)l=INT16_MIN;if(i%37==0)r=INT16_MAX;
            usb_input_wave_push(l*256,r*256);old_push(l*256,r*256);
            if(i%1021==0)identical();
            if(i%8191==0){usb_input_wave_stream_start();frozen_filled=0;}
        }
        identical();
    }
    puts("PASS: optimized producer bit-exact vs frozen 0.0.38: 1.2M stereo frames, all seven histories/partial accumulators, extrema, restart, count wrap and filled saturation");
}
