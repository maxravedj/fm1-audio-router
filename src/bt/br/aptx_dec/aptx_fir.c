/* SPDX-License-Identifier: MPL-2.0 */
/* Compiled separately at O2. No allocator or platform calls in the tap loop. */
#include "aptx_fir.h"
#ifdef __PI32V2__
/* Register-lane stereo MAC kernel; the host version below is the exact scalar
 * equivalent. BSP assembly preserves all callee registers and never allocates. */
extern void fm1_aptx_fir24_span(const int16_t high[][2],const uint8_t low[][2],
        const int16_t *coeff,uint32_t frac,unsigned count,int32_t out[4]);
#endif
/* Bound each partial to one half. Split at the ring wrap once, rather than
 * masking/indexing both PCM arrays on every tap. The target's register-lane
 * kernel and host scalar loop implement exactly the same four partial sums. */
static __attribute__((noinline)) void aptx_fir24_half(
        const int16_t high[2048][2],const uint8_t low[2048][2],
        uint32_t at,const int16_t *coeff,uint32_t frac,int32_t out[4])
{
    int32_t hl=0,hr=0,ll=0,lr=0;
    /* The first/last three taps of BOTH 65-phase tables are exactly zero.
     * Their omission is covered by the full 96-tap independent oracle. */
    unsigned remaining=45,pos=at&2047u;
    do{
        unsigned run=2048-pos;if(run>remaining)run=remaining;
        const int16_t (*hp)[2]=high+pos;const uint8_t (*lp)[2]=low+pos;
        remaining-=run;
#ifdef __PI32V2__
        int32_t part[4];
        fm1_aptx_fir24_span(hp,lp,coeff,frac,run,part);
        hl+=part[0];hr+=part[1];ll+=part[2];lr+=part[3];coeff+=run;
#else
#define FIR24_TAP() do { \
            int32_t d=(coeff[96]-coeff[0])*(int32_t)frac+512; \
            int32_t c=coeff[0]+((d+(d<0?1023:0))>>10); \
            hl+=(int32_t)(*hp)[0]*c;hr+=(int32_t)(*hp)[1]*c; \
            ll+=(int32_t)(*lp)[0]*c;lr+=(int32_t)(*lp)[1]*c; \
            hp++;lp++;coeff++; \
        }while(0)
        if(run&1u)FIR24_TAP();
        for(run>>=1;run;run--){FIR24_TAP();FIR24_TAP();}
#undef FIR24_TAP
#endif
        pos=0;
    }while(remaining);
    out[0]=hl;out[1]=hr;out[2]=ll;out[3]=lr;
}
void btr_fir24(const int16_t high[2048][2],const uint8_t low[2048][2],
               uint32_t at,const int16_t coeff[192],uint32_t frac,int32_t out[2])
{
    int32_t a[4],b[4];
    aptx_fir24_half(high,low,at+3,coeff+3,frac,a);
    aptx_fir24_half(high,low,at+48,coeff+48,frac,b);
    /* Per-half absolute-coefficient bound 50573, including interpolation:
     * high sums fit signed 32 bits, low sums <=255*50573. Widen after each
     * half; round only once. */
    out[0]=(int32_t)(((int64_t)a[0]*256+a[2]+(int64_t)b[0]*256+b[2])>>15);
    out[1]=(int32_t)(((int64_t)a[1]*256+a[3]+(int64_t)b[1]*256+b[3])>>15);
}
