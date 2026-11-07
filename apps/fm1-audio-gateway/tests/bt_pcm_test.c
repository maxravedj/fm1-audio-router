/* SPDX-License-Identifier: MPL-2.0 */
/* Mixer CPU optimization: exact legacy arithmetic, overflow bounds and timing.
 * No SBC/profile/UI tests: those components are unchanged. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define APP_BT_RECEIVER
#include "../../../bt/br/bt_receiver_pcm.c"
#include "bt_pcm_reference.h"
static uint32_t random_state=901;
static uint32_t random32(void) {return random_state=random_state*1664525u+1013904223u;}
static void snapshot(void)
{
    reference.r=btr_r;reference.phase=btr_phase;reference.step=btr_step;
    reference.servo=btr_servo;reference.primed=btr_primed;reference.stats=btr_stats;
}
static void compare(unsigned n,unsigned gain)
{
    int32_t old[512],now[512];assert(n<=256);
    for(unsigned i=0;i<2*n;i++)old[i]=now[i]=(int32_t)(random32()%2000001)-1000000;
    snapshot();reference_mix(old,n,gain);btr_pcm_mix(now,n,gain);
    assert(!memcmp(old,now,2*n*sizeof *old));
    assert(reference.r==btr_r && reference.phase==btr_phase && reference.step==btr_step &&
           reference.servo==btr_servo && reference.primed==btr_primed);
    assert(!memcmp(&reference.stats,&btr_stats,sizeof btr_stats));
}
static void bounds(void)
{
    for(unsigned rate=44100;rate<=48000;rate+=3900) {
        const int16_t (*filter)[96]=rate==48000?BTR_FIR_48:BTR_FIR_44;
        unsigned worst=0;
        for(unsigned phase=0;phase<=64;phase++) for(unsigned half=0;half<96;half+=48) {
            unsigned norm=48; /* conservative one-unit interpolation error/tap */
            for(unsigned k=half;k<half+48;k++)norm+=abs(filter[phase][k]);
            assert((int64_t)norm*32768<INT32_MAX);
            assert((int64_t)norm*2*4096<INT32_MAX);
            if(norm>worst)worst=norm;
        }
        printf("PASS: %u-Hz filter bound %u/half; full-scale PCM and Q12 gain cannot overflow int32\n",rate,worst);
    }
}
static void all_phases(void)
{
    for(unsigned rate=44100;rate<=48000;rate+=3900) {
        btr_audio_reset(rate);
        for(unsigned k=0;k<BTR_RING;k++)for(unsigned ch=0;ch<2;ch++)btr_pcm[k][ch]=(int16_t)random32();
        for(unsigned phase=0;phase<65536;phase++) {
            btr_r=phase*13;btr_w=btr_r+BTR_TARGET;btr_phase=phase;btr_servo=0;btr_primed=1;
            compare(1,phase%4097);
        }
    }
    puts("PASS: all 65536 fractional phases at both rates, stereo random PCM, every gain, ring wrap: bit-exact with 0.0.17");
}
static void extremes(void)
{
    for(unsigned rate=44100;rate<=48000;rate+=3900) for(unsigned phase=0;phase<64;phase++) {
        const int16_t (*filter)[96]=rate==48000?BTR_FIR_48:BTR_FIR_44;
        btr_audio_reset(rate);btr_r=8170;btr_w=btr_r+BTR_TARGET;btr_phase=phase*1024+511;btr_servo=0;btr_primed=1;
        for(unsigned k=0;k<96;k++) {
            int32_t d=(filter[phase+1][k]-filter[phase][k])*511+512;
            int32_t c=filter[phase][k]+d/1024;
            btr_pcm[(btr_r+k)&(BTR_RING-1)][0]=c<0?INT16_MIN:INT16_MAX;
            btr_pcm[(btr_r+k)&(BTR_RING-1)][1]=c<0?INT16_MAX:INT16_MIN;
        }
        compare(1,4096);
    }
    for(unsigned rate=44100;rate<=48000;rate+=3900) for(unsigned pattern=0;pattern<4;pattern++) {
        btr_audio_reset(rate);btr_r=0xfffffff0u;btr_w=btr_r+BTR_TARGET;btr_phase=65535;btr_primed=1;
        for(unsigned k=0;k<BTR_RING;k++) {
            int16_t v=pattern==0?INT16_MIN:pattern==1?INT16_MAX:pattern==2?(k&1?INT16_MIN:INT16_MAX):0;
            btr_pcm[k][0]=v;btr_pcm[k][1]=v;
        }
        compare(256,4096);
    }
    puts("PASS: worst-sign filter inputs, full-scale DC/Nyquist/silence and uint32 index rollover: bit-exact");
}
static void streaming(void)
{
    static int16_t in[512];
    for(unsigned rate=44100;rate<=48000;rate+=3900) {
        btr_audio_reset(rate);memset(&btr_stats,0,sizeof btr_stats);
        compare(256,4096);assert(!btr_primed);
        for(unsigned k=0;k<2*BTR_TARGET;k++)in[k%512]=(int16_t)random32();
        for(unsigned k=0;k<BTR_TARGET/256;k++)btr_pcm_put(in,256);
        for(unsigned block=0;block<3000;block++) {
            compare(1+random32()%256,block%127==0?0:random32()%4097);
            /* Exercise both servo limits and reprime after irregular producer
             * bursts; compare output and every consumption/servo/error state. */
            if(block%300==0){btr_r=btr_w-97;compare(256,4096);assert(!btr_primed);}
            if(btr_w-btr_r<BTR_TARGET+(block&1?512:0)) {
                for(unsigned k=0;k<512;k++)in[k]=(int16_t)random32();
                btr_pcm_put(in,256);
            }
            if(block%701==0) {btr_pcm_put(in,BTR_RING);assert(btr_stats.overruns);}
        }
        btr_audio_reset(rate);compare(256,0);assert(!btr_primed);
    }
    puts("PASS: jitter bursts, ±servo correction, block/gain changes, mute, priming, underrun/overrun and reset: bit-exact");
}
static volatile int32_t sink;
static double benchmark(int old)
{
    struct timespec begin,end;int32_t out[512];
    btr_audio_reset(48000);btr_w=BTR_TARGET;btr_primed=1;
    for(unsigned k=0;k<BTR_RING;k++)for(unsigned c=0;c<2;c++)btr_pcm[k][c]=(int16_t)random32();
    snapshot();clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&begin);
    for(unsigned block=0;block<12000;block++) {
        memset(out,0,sizeof out);
        if(old){btr_w=reference.r+BTR_TARGET;reference_mix(out,256,3031);}
        else {btr_w=btr_r+BTR_TARGET;btr_pcm_mix(out,256,3031);}
        sink=out[0];
    }
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&end);
    return end.tv_sec-begin.tv_sec+(end.tv_nsec-begin.tv_nsec)*1e-9;
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--bench")) {
        double old=benchmark(1),now=benchmark(0);
        printf("HOST ONLY (not FM-1 cycles): 3072000 frames, reference %.3f s, optimized %.3f s, ratio %.2fx\n",old,now,old/now);
        return 0;
    }
    bounds();all_phases();extremes();streaming();
}
