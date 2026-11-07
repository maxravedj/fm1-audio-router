/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "plat.h"
static unsigned locked;
static uint32_t test_ms;
uint32_t plat_ms(void) { return test_ms; }
uint32_t plat_lock(void) { assert(!locked); locked = 1; return 0; }
void plat_unlock(uint32_t n) { (void)n; assert(locked); locked = 0; }
#include "../../../device/usb_input_waveform.h"
static int16_t wave[240][4], reference[240][4];
static void reset(uint32_t count)
{
    memset((void *)usb_wave_history, 0, sizeof(usb_wave_history));
    memset(usb_wave_accum, 0, sizeof(usb_wave_accum));
    usb_wave_count = count; usb_wave_filled = 0;
}
/* Frozen 0.0.37 per-column partition as an independent optimization oracle. */
static void old_capture(int16_t out[240][4],unsigned width,unsigned span,unsigned end)
{
    unsigned level=0;
    while(level+1<USB_WAVE_LEVELS && (1u<<(2*(level+1)))<=span/width)level++;
    unsigned stride=1u<<(2*level);end&=~(stride-1);
    unsigned begin=end-span;
    for(unsigned x=0;x<width;x++) {
        unsigned first=begin+x*span/width,last=begin+(x+1)*span/width;
        int have=0;if(last==first)last++;first&=~(stride-1);
        unsigned lock=plat_lock();
        for(unsigned frame=first;(int32_t)(frame-last)<0;frame+=stride) {
            int16_t v[4]={0};usb_wave_read(frame,level,v);
            for(unsigned k=0;k<4;k+=2) {
                if(!have || v[k]<out[x][k])out[x][k]=v[k];
                if(!have || v[k+1]>out[x][k+1])out[x][k+1]=v[k+1];
            }
            have=1;
        }
        plat_unlock(lock);
    }
}
int main(void)
{
    unsigned i, j, z;
    const unsigned spans[] = {176,352,705,1411,2822,5644,11289,22579,44100,88200,176400,352800};
    reset(0);
    /* Eight seconds of distinct L/R DC with isolated full-scale impulses.
     * Compare every resolution's extrema to the exact source bin interval. */
    for (i = 0; i < 400000u; i++)
        usb_input_wave_push(i == 399000u ? -8388608 : 25600,
                            i == 398999u ? 8388352 : -51200);
    assert(plat_usb_input_wave_position() == 400000u);
    for (z = 0; z < sizeof(spans)/sizeof(spans[0]); z++) {
        unsigned level = 0, stride, end, begin;
        while (level + 1u < USB_WAVE_LEVELS && (1u << (2u*(level+1u))) <= spans[z]/126u) level++;
        stride = 1u << (2u*level); end = 400000u & ~(stride-1u); begin = end-spans[z];
        plat_usb_input_waveform(wave,126,spans[z],400000);
        for (i = 0; i < 126u; i++) {
            unsigned first = begin + i*spans[z]/126u, last = begin+(i+1u)*spans[z]/126u;
            int16_t expected[4] = {100,100,-200,-200};
            if (first == last) last++;
            first &= ~(stride-1u); last = (last+stride-1u)&~(stride-1u);
            if (first <= 399000u && last > 399000u) expected[0] = -32768;
            if (first <= 398999u && last > 398999u) expected[3] = 32767;
            for (j=0;j<4;j++) assert(wave[i][j] == expected[j]);
        }
    }
    /* Scrolling moves an impulse left as new audio arrives, then discards it. */
    reset(0);
    for(i=0;i<352800;i++) usb_input_wave_push(i==330000 ? 8388352 : 0,0);
    plat_usb_input_waveform(reference,126,352800,usb_wave_count);
    for(i=0;i<44100;i++) usb_input_wave_push(0,0);
    plat_usb_input_waveform(wave,126,352800,usb_wave_count);
    { int a=-1,b=-1; for(i=0;i<126;i++) {if(reference[i][1])a=i;if(wave[i][1])b=i;} assert(b>=0 && b<a); }
    plat_usb_input_waveform(wave,126,352800,usb_wave_count+353824u);
    for(i=0;i<126;i++)for(j=0;j<4;j++)assert(!wave[i][j]);
    /* Restart at a deliberately unaligned frame: no coarse min/max bin may
     * leak old negative-L / positive-R data into the new stream. */
    reset(0);
    for(i=0;i<400037;i++)usb_input_wave_push(-7680000,7680000);
    usb_input_wave_stream_start();
    assert(usb_wave_count==400037 && usb_wave_filled==0);
    for(i=0;i<4321;i++)usb_input_wave_push(25600,-51200);
    for(z=0;z<sizeof(spans)/sizeof(spans[0]);z++) {
        plat_usb_input_waveform(wave,126,spans[z],usb_wave_count);
        for(i=0;i<126;i++) {
            assert(wave[i][0]>=0 && wave[i][1]<=100);
            assert(wave[i][2]>=-200 && wave[i][3]<=0);
        }
    }
    /* Packet gaps invalidate even when the host resumes without a stream
     * start event. Ordinary short gaps retain history; time wrap is safe. */
    usb_wave_packet_seen=0;test_ms=1000;usb_input_wave_packet_begin();
    test_ms=1099;usb_input_wave_packet_begin();assert(usb_wave_filled==4321);
    test_ms=1199;usb_input_wave_packet_begin();assert(usb_wave_filled==0);
    usb_input_wave_push(0,0);test_ms=4294967246u;usb_input_wave_packet_begin();
    usb_input_wave_push(0,0);test_ms=50;usb_input_wave_packet_begin();assert(usb_wave_filled==0);
    /* All resolutions remain coherent across the 32-bit sample counter wrap. */
    reset(4294963200u);
    for(i=0;i<400000;i++)usb_input_wave_push(25600,-51200);
    for(z=0;z<sizeof(spans)/sizeof(spans[0]);z++) {
        plat_usb_input_waveform(wave,126,spans[z],usb_wave_count);
        for(i=0;i<126;i++) assert(wave[i][0]==100 && wave[i][1]==100 && wave[i][2]==-200 && wave[i][3]==-200);
    }
    uint32_t random=117;
    for(i=0;i<4096;i++) {
        random=random*1664525u+1013904223u;int32_t l=(int16_t)random;
        random=random*1664525u+1013904223u;int32_t r=(int16_t)random;
        usb_input_wave_push(l*256,r*256);
    }
    for(unsigned width=1;width<=240;width++)for(z=0;z<12;z++)for(unsigned idle=0;idle<2;idle++) {
        unsigned end=usb_wave_count+(idle?383241:0);
        old_capture(reference,width,spans[z],end);
        plat_usb_input_waveform(wave,width,spans[z],end);
        assert(!memcmp(reference,wave,width*sizeof *wave));
    }
    puts("PASS: exact DDA capture vs frozen per-column division at all 240 widths/12 windows, wrapped counters and idle silence");
    puts("PASS: 12 untriggered scales vs exact extrema, transient retention, scrolling, restart invalidation including partial bins, packet gaps and counter wrap");
}
