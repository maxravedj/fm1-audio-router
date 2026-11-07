/* SPDX-License-Identifier: MPL-2.0
 * Portable master EQ/peak limiter. Coefficients are prepared in foreground;
 * the audio owner alone mutates filter, delay and detector histories.
 * Trapezoidal SVF: Andrew Simper, Cytomic (public-domain technical papers).
 * https://www.cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
 * Responses cross-checked against https://www.w3.org/TR/audio-eq-cookbook/ */
#pragma once
#include <stdint.h>
#ifdef PLAT_HOST
#include <string.h>
#endif
#include "fastmath.h"
#include "limiter_ceiling.h"
#define FX_BANDS 5u
#define FX_STAGES 4u
#define FX_DELAY 128u
#define FX_FADE 441u
#define FX_BYPASS_FADE 221u /* 5.009 ms at the actual ALNK rate, each way. */
#define FX_RAMP (FX_DELAY/2u+1u)
#define FX_GAIN_ONE 16777216u /* Q24: two 65-point sums fit uint32_t exactly. */
#define FX_RATE 44117.647f /* ALNK0 192 MHz / 17 / 256; host uses the same DSP. */
enum { FX_BELL, FX_LOW_PASS, FX_HIGH_PASS, FX_LOW_SHELF, FX_HIGH_SHELF, FX_TYPES };
typedef struct { uint16_t hz; int16_t gain; uint16_t q; uint8_t type,on; } fx_band_t;
typedef struct {
    uint8_t chain,eq,lim,reserved;
    int16_t post,input,ceiling;
    uint16_t release;
    fx_band_t band[FX_BANDS];
    uint8_t slope[FX_BANDS],tail; /* 0..3 -> 12/24/36/48 dB/oct; v1 prefix retained. */
} fx_params_t;
typedef struct { float a1,a2,a3,m0,m1,m2; } fx_coef_t;
typedef struct { fx_coef_t c[FX_BANDS][FX_STAGES]; uint8_t stages[FX_BANDS]; float post,input,ceiling,release; uint32_t mask,lim; } fx_bank_t;
typedef struct {
    fx_bank_t bank[2],pending;
    volatile uint32_t generation;
    uint32_t seen,fade,active,at,time,head,count;
    float z[2][FX_BANDS][FX_STAGES][2][2];
    float delay[FX_DELAY][2];
    float peaks[FX_DELAY+1u]; uint32_t stamps[FX_DELAY+1u];
    uint32_t ramp[2][FX_RAMP],ramp_sum[2],ramp_at;
    uint32_t latency_on,bypass_level,detector_on;
    float gain,release_gain;
    volatile uint32_t reduction_q16,faults,output_peak[2],meter_reduction;
    volatile uint32_t meter_enabled;
#ifdef APP_USB_OUT_ROUTING
    int32_t *capture; /* optional pre-MASTER, headroom recording copy */
#endif
} fx_engine_t;
static void fx_defaults(fx_params_t *p)
{
    static const uint16_t hz[]={80,250,1000,4000,12000};
    memset(p,0,sizeof *p);
    /* Fresh installs bypass the chain, with both effects ready to enable. */
    p->chain=0;p->eq=p->lim=1;p->ceiling=-10;p->release=100;
    for(unsigned i=0;i<FX_BANDS;i++)p->band[i]=(fx_band_t){hz[i],0,71,FX_BELL,1};
}
static int fx_valid(const fx_params_t *p)
{
    if(p->tail)return 0;
    if(p->chain>1 || p->eq>1 || p->lim>1 || p->reserved || p->post< -240 || p->post>120 ||
       p->input< -240 || p->input>240 || p->ceiling< -240 || p->ceiling>0 || p->release<10 || p->release>1000)return 0;
    for(unsigned i=0;i<FX_BANDS;i++){
        const fx_band_t *b=&p->band[i];
        if(b->hz<20 || b->hz>19850 || b->gain< -240 || b->gain>240 || b->q<10 || b->q>1800 || b->type>=FX_TYPES || b->on>1 || p->slope[i]>=FX_STAGES)return 0;
    }
    return 1;
}
typedef struct { float g,k,m0,m1,m2; } fx_shape_t;
static fx_shape_t fx_shape(const fx_band_t *b,float q)
{
    float g=fm_tanf(FM_PI*(float)b->hz/FX_RATE),k=1.0f/q;
    float A=fm_db2lin((float)b->gain*0.05f),m0=1.0f,m1=0.0f,m2=0.0f;
    switch(b->type){
    case FX_LOW_PASS:m0=0;m2=1;break;
    case FX_HIGH_PASS:m1=-k;m2=-1;break;
    case FX_LOW_SHELF:g/=fm_sqrtf(A);m1=k*(A-1);m2=A*A-1;break;
    case FX_HIGH_SHELF:g*=fm_sqrtf(A);m0=A*A;m1=k*(1-A)*A;m2=1-A*A;break;
    default:k/=A;m1=k*(A*A-1);break;
    }
    return (fx_shape_t){g,k,m0,m1,m2};
}
static int fx_coeff_q(const fx_band_t *b,float q,fx_coef_t *o)
{
    fx_shape_t s=fx_shape(b,q);
    float a1=1.0f/(1.0f+s.g*(s.g+s.k)),a2=s.g*a1,a3=s.g*a2;
    *o=(fx_coef_t){a1,a2,a3,s.m0,s.m1,s.m2};
    return s.g>0 && s.k>0 && a1>0 && a1<1 && a3>0 && a3<1;
}
static int fx_coeff(const fx_band_t *b,fx_coef_t *o){return fx_coeff_q(b,(float)b->q*0.01f,o);}
static unsigned fx_stages(const fx_params_t *p,unsigned i)
{
    return p->band[i].type==FX_LOW_PASS || p->band[i].type==FX_HIGH_PASS ? p->slope[i]+1u : 1u;
}
static float fx_stage_q(const fx_band_t *b,unsigned count,unsigned stage)
{
    /* Butterworth pole pairs, ascending Q. User resonance scales only the
     * first pair; 0.71 yields the maximally-flat higher-order pass response. */
    static const float q[4][4]={{0.71f,0,0,0},{0.5411961f,1.3065630f,0,0},
        {0.5176381f,0.7071068f,1.9318517f,0},{0.5097956f,0.6013449f,0.8999762f,2.5629154f}};
    if(count==1)return (float)b->q*0.01f;
    float value=q[count-1][stage];
    return stage?value:value*(float)b->q*(1.0f/71.0f);
}
static int fx_build(const fx_params_t *p,fx_bank_t *b)
{
    if(!fx_valid(p))return 0;
    memset(b,0,sizeof *b);b->post=b->input=1.0f;
    for(unsigned i=0;i<FX_BANDS;i++){
        b->stages[i]=fx_stages(p,i);b->c[i][0].m0=1;
        if(p->chain && p->eq && p->band[i].on && (p->band[i].gain || p->band[i].type==FX_LOW_PASS || p->band[i].type==FX_HIGH_PASS)){
            for(unsigned j=0;j<b->stages[i];j++)
                if(!fx_coeff_q(&p->band[i],fx_stage_q(&p->band[i],b->stages[i],j),&b->c[i][j]))return 0;
            b->mask|=1u<<i;
        }
    }
    if(p->chain && p->eq)b->post=fm_db2lin((float)p->post*0.1f);
    b->lim=p->chain && p->lim;
    if(b->lim)b->input=fm_db2lin((float)p->input*0.1f);
    b->ceiling=(float)fx_ceiling_q23[-p->ceiling]*(1.0f/8388608.0f);
    b->release=1.0f-fm_expf(-1000.0f/(FX_RATE*(float)p->release));
    return 1;
}
static void fx_detector_reset(fx_engine_t *e)
{
    e->head=e->count=e->ramp_at=0;e->gain=e->release_gain=1.0f;
    for(unsigned k=0;k<2;k++){
        e->ramp_sum[k]=FX_RAMP*FX_GAIN_ONE;
        for(unsigned j=0;j<FX_RAMP;j++)e->ramp[k][j]=FX_GAIN_ONE;
    }
}
static void fx_init(fx_engine_t *e,const fx_bank_t *b)
{
    memset(e,0,sizeof *e);
    memcpy(&e->bank[0],b,sizeof *b);memcpy(&e->bank[1],b,sizeof *b);memcpy(&e->pending,b,sizeof *b);
    fx_detector_reset(e);e->latency_on=e->detector_on=b->lim;
    e->bypass_level=FX_BYPASS_FADE;e->reduction_q16=65536u;
}
static float fx_filter(fx_engine_t *e,unsigned k,unsigned ch,float in)
{
    const fx_bank_t *b=&e->bank[k];float x=in;
    if(!b->mask)return x*b->post*b->input;
    for(unsigned i=0;i<FX_BANDS;i++)if(b->mask&(1u<<i)){
        for(unsigned j=0;j<b->stages[i];j++){
        const fx_coef_t *c=&b->c[i][j];float *z=e->z[k][i][j][ch];
        float v3=x-z[1],v1=c->a1*z[0]+c->a2*v3;
        float v2=z[1]+c->a2*z[0]+c->a3*v3;
        z[0]=2.0f*v1-z[0];z[1]=2.0f*v2-z[1];
        x=c->m0*x+c->m1*v1+c->m2*v2;
        }
    }
    return x*b->post*b->input;
}
static int fx_finite(float x){return x>= -1.0e12f && x<=1.0e12f;}
/* Q12-scaled float accepts precise low-level MASTER gain as well as the
 * original integer calls (4096 is unity). DSP state/processing are unchanged. */
static void fx_process(fx_engine_t *e,int32_t *pcm,uint32_t n,float master_gain)
{
    /* Pending is copied by the audio owner only after the preceding transition. */
    if(!e->fade && e->seen!=e->generation){
        unsigned next=e->active^1u;e->bank[next]=e->pending;e->seen=e->generation;
        /* Limiter/post-gain edits leave the upstream EQ path unchanged.
         * Its warm state must follow the bank, rather than injecting a new
         * filter startup transient into an otherwise transparent edit. */
        const fx_bank_t *a=&e->bank[e->active],*b=&e->bank[next];
        if(a->mask==b->mask && !memcmp(a->stages,b->stages,sizeof a->stages) &&
           !memcmp(a->c,b->c,sizeof a->c))memcpy(e->z[next],e->z[e->active],sizeof e->z[next]);
        else memset(e->z[next],0,sizeof e->z[next]);
        e->fade=FX_FADE;
    }
    float minimum=1.0f,master=(float)master_gain*(1.0f/4096.0f);
    for(unsigned i=0;i<n;i++){
        float x[2],delayed[2];unsigned k=e->active;
        for(unsigned ch=0;ch<2;ch++){
            float in=(float)pcm[2*i+ch]*(1.0f/8388608.0f);
            x[ch]=fx_filter(e,k,ch,in);
            if(e->fade){float next=fx_filter(e,k^1u,ch,in);float mix=(float)(FX_FADE-e->fade+1u)*(1.0f/FX_FADE);x[ch]+=mix*(next-x[ch]);}
            if(!fx_finite(x[ch])){x[ch]=0;memset(e->z,0,sizeof e->z);e->faults++;}
            delayed[ch]=e->delay[e->at][ch];e->delay[e->at][ch]=x[ch];
        }
        unsigned target=e->fade?k^1u:k;
        const fx_bank_t *b=&e->bank[target];
        /* Fade to exact silence before changing time alignment. The delay
         * stays warm even in bypass; no interpolation of time or dry/wet comb.
         * Reversing a request reverses the existing envelope, without jumps. */
        if(b->lim!=e->latency_on){
            if(e->bypass_level)e->bypass_level--;
            if(!e->bypass_level)e->latency_on=b->lim;
        }else if(e->bypass_level<FX_BYPASS_FADE)e->bypass_level++;
        float g=1.0f;
        unsigned detecting=b->lim || e->latency_on;
        if(detecting){
        if(!e->detector_on)fx_detector_reset(e);
        e->detector_on=1;
        float peak=fm_fabsf(x[0]);if(fm_fabsf(x[1])>peak)peak=fm_fabsf(x[1]);
        /* Window includes output sample and all 128 future samples. */
        while(e->count && e->time-e->stamps[e->head]>FX_DELAY){if(++e->head>FX_DELAY)e->head=0;e->count--;}
        while(e->count){unsigned end=e->head+e->count-1u;if(end>FX_DELAY)end-=FX_DELAY+1u;
            if(e->peaks[end]>peak)break;
            e->count--;}
        unsigned end=e->head+e->count;if(end>FX_DELAY)end-=FX_DELAY+1u;
        e->peaks[end]=peak;e->stamps[end]=e->time;e->count++;
        float ceiling=b->ceiling,window=e->peaks[e->head];
        float required=window>ceiling?ceiling/window:1.0f;
        /* Existing exponential release, followed by a finite S-shaped attack.
         * Two 65-point box averages have a triangular, nonnegative kernel of
         * support 129 (128 intervals). Its integrated step is a curved ramp,
         * flat at both ends. Every contributing gain is <= a peak's required
         * gain at that peak's output deadline: overlapping peaks cannot move
         * an earlier deadline or cause a reset/jump. Isolated peaks reach the
         * target exactly (within Q24). Rolling integer sums cannot drift.
         * All storage is fixed; updates O(1), peak deque amortized O(1). */
        e->release_gain+=(1.0f-e->release_gain)*b->release;
        if(e->release_gain>required)e->release_gain=required;
        uint32_t v=(uint32_t)(e->release_gain*(float)FX_GAIN_ONE);
        for(unsigned j=0;j<2;j++){
            e->ramp_sum[j]-=e->ramp[j][e->ramp_at];
            e->ramp[j][e->ramp_at]=v;e->ramp_sum[j]+=v;
            v=e->ramp_sum[j]/FX_RAMP;
        }
        if(++e->ramp_at==FX_RAMP)e->ramp_at=0;
        e->gain=(float)v*(1.0f/FX_GAIN_ONE);
        g=e->latency_on?e->gain:1.0f;
        /* Rounding and live parameter/startup safety only. Steady-state
         * attenuation has already reached its target through anticipation. */
        /* Warm detection during bypass fade-out, but constrain only the
         * active limited path. Applying the pending ceiling to dry output
         * would make an abrupt gain step before the silent tap switch. */
        if(e->latency_on){
            float p=fm_fabsf(delayed[0]);if(fm_fabsf(delayed[1])>p)p=fm_fabsf(delayed[1]);
            if(p*g>ceiling)g=ceiling/p;
        }
        if(b->lim && g<minimum)minimum=g;
        }else{e->detector_on=0;e->head=e->count=0;e->gain=1.0f;}
        float switch_gain=(float)e->bypass_level*(1.0f/FX_BYPASS_FADE);
        for(unsigned ch=0;ch<2;ch++){
            float post=(e->latency_on?delayed[ch]:x[ch])*g*8388608.0f;
            /* Strict integer sample ceiling, including float rounding at the
             * last LSB. Stereo gain remains linked apart from this <=1 LSB. */
            if(e->latency_on){float cap=(float)(uint32_t)(b->ceiling*8388608.0f);
                if(post>cap)post=cap;if(post< -cap)post= -cap;}
            if(e->meter_enabled){uint32_t peak=(uint32_t)(fm_fabsf(post)>16777216.0f?16777216.0f:fm_fabsf(post));if(peak>e->output_peak[ch])e->output_peak[ch]=peak;}
            post*=switch_gain; /* Do not display the bypass transition as GR. */
#ifdef APP_USB_OUT_ROUTING
            if(e->capture){float c=post;if(c>1073741824.0f)c=1073741824.0f;if(c< -1073741824.0f)c= -1073741824.0f;
                e->capture[2*i+ch]=(int32_t)c;}
#endif
            float y=post*master;
            if(y>8388607.0f)y=8388607.0f;
            if(y< -8388608.0f)y= -8388608.0f;
#ifdef APP_USB_OUT_ROUTING
            /* In-place source recording keeps headroom until its OUT fader. */
            if(e->capture!=pcm)
#endif
            pcm[2*i+ch]=(int32_t)y;

        }
        e->at=(e->at+1u)&(FX_DELAY-1u);e->time++;
        if(e->fade && !--e->fade)e->active^=1u;
    }
    e->reduction_q16=(uint32_t)(minimum*65536.0f);
    if(e->meter_enabled && e->reduction_q16<e->meter_reduction)e->meter_reduction=e->reduction_q16;
}
