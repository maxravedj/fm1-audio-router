/* SPDX-License-Identifier: MPL-2.0
 * Independent numerical/safety and causal streaming checks for both dynamics. */
#define PLAT_HOST
#include "../fx/master_fx.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static uint32_t noise=1;
static uint32_t random_word(void){noise=noise*1664525u+1013904223u;return noise;}
static void setup(fx_params_t *p,fx_bank_t *b,fx_engine_t *e)
{
    fx_defaults(p);p->chain=1;p->eq=0;p->ceiling=-60;
    assert(fx_build(p,b));b->dynamics=FX_DYNAMICS_LM;fx_init(e,b);
}
static void latency_and_ceiling(void)
{
    fx_params_t p;fx_bank_t b;fx_engine_t e;setup(&p,&b,&e);
    for(unsigned i=0;i<200;i++){
        int32_t pcm[2]={i?0:10000,i?0:-5000};fx_process(&e,pcm,1,4096);
        assert(pcm[0]==(i==LM_LOOK?10000:0) && pcm[1]==(i==LM_LOOK?-5000:0));
    }
    p.input=300;assert(fx_build(&p,&b) && fabs(b.input-pow(10.,30./20))<.0001);
    p.input=301;assert(!fx_build(&p,&b));
    for(unsigned tenth=0;tenth<=240;tenth++){
        p.ceiling=-(int)tenth;p.input=300;assert(fx_build(&p,&b));b.dynamics=FX_DYNAMICS_LM;fx_init(&e,&b);
        int cap=(int)floor(pow(10.,-(double)tenth/200.)*8388608.);
        for(unsigned i=0;i<2000;i++){
            int32_t pcm[2]={(int32_t)(random_word()%1000000000u)-500000000,0};pcm[1]=-pcm[0]/2;
            fx_process(&e,pcm,1,4096);
            assert(abs(pcm[0])<=cap && abs(pcm[1])<=cap && abs(pcm[0]/2+pcm[1])<=2);
            assert(!e.faults && e.count<=LM_LOOK+1 && e.gain>0 && e.gain<=1);
        }
    }
    setup(&p,&b,&e);
    for(unsigned i=0;i<25000;i++){
        int32_t pcm[2]={i==23000?16000000:1000,500};fx_process(&e,pcm,1,4096);
        if(i==23000+LM_LOOK)assert(e.gain*(16000000.f/8388608.f)<=b.ceiling+0.000001f);
    }
    puts("PASS: LM 55-frame latency, anticipated isolated peak, all 241 strict integer ceilings with +30 dB drive/headroom and linked stereo");
}
static void streaming_and_isolation(void)
{
    enum { FRAMES=35000 };static int32_t a[2*FRAMES],bpcm[2*FRAMES];
    fx_params_t p;fx_bank_t b;fx_engine_t x,y,clean;setup(&p,&b,&x);y=x;clean=x;
    for(unsigned i=0;i<2*FRAMES;i++)a[i]=bpcm[i]=(int32_t)(random_word()%40000000)-20000000;
    fx_process(&x,a,FRAMES,4096);
    for(unsigned at=0;at<FRAMES;){unsigned n=1+random_word()%256;if(n>FRAMES-at)n=FRAMES-at;fx_process(&y,bpcm+2*at,n,4096);at+=n;}
    assert(!memcmp(a,bpcm,sizeof a));
    for(unsigned i=0;i<1000;i++){int32_t f[2]={10000,-5000};fx_process(&clean,f,1,4096);if(i>=LM_LOOK)assert(f[0]==10000 && f[1]==-5000);}
    setup(&p,&b,&x);x.time=UINT32_MAX-20;
    for(unsigned i=0;i<1000;i++){int32_t f[2]={16000000,-8000000};fx_process(&x,f,1,4096);assert(abs(f[0])<=(int)fx_ceiling_q23[60]);}
    /* Automatic release ignores the Peak Limiter's stored release control. */
    p.release=10;assert(fx_build(&p,&b));b.dynamics=FX_DYNAMICS_LM;fx_init(&x,&b);
    p.release=1000;assert(fx_build(&p,&b));b.dynamics=FX_DYNAMICS_LM;fx_init(&y,&b);
    for(unsigned i=0;i<200000;i++){
        int32_t f[2]={i<15000?7000000:0,-1000},g[2];memcpy(g,f,sizeof g);
        fx_process(&x,f,1,4096);fx_process(&y,g,1,4096);assert(!memcmp(f,g,sizeof f));
    }
    assert(x.gain>.9999f && !x.detector.lm.captured);
    puts("PASS: LM arbitrary block partitions identical across byte/32-bit timestamp wraps; independent routes, automatic release independent of saved release, long-tail silence recovery");
}
static void release_oracle(void)
{
    lm_state_t s;lm_reset(&s);s.captured=10;s.long_env=8;
    const double floor=.16417916258046156*10,rate=3.2490891815774225;
    for(unsigned tick=0;tick<100;tick++){
        for(unsigned i=0;i<LM_LOOK;i++)lm_gain(&s,0,1);
        double expected=floor+(8-floor)*exp(-rate*(tick+1)*LM_LOOK/FX_RATE);
        assert(fabs(s.long_env-expected)<.0001);
    }
    s.stage=1;s.middle_age=0;s.long_env=3;
    for(unsigned tick=0;tick<397;tick++){
        for(unsigned i=0;i<LM_LOOK;i++)lm_gain(&s,0,1);
        double expected=floor+(3-floor)*pow(.9968263346512797,tick+1);
        assert(s.stage==1 && fabs(s.long_env-expected)<.0001);
    }
    for(unsigned i=0;i<LM_LOOK;i++)lm_gain(&s,0,1);assert(s.stage==2);
    lm_reset(&s);s.captured=10;s.long_env=3;s.stage=1;s.middle_age=380;s.pause=LM_PAUSE;
    for(unsigned i=0;i<20*LM_LOOK;i++)lm_gain(&s,0,1);
    assert(s.stage==2 && s.long_env==3 && s.pause>0); /* Age is not decay. */
    puts("PASS: early/middle release against independent double recurrences; half-second middle age and quiet-pause independence");
}
static void mode_switches(void)
{
    fx_params_t p;fx_bank_t b;fx_engine_t e;setup(&p,&b,&e);
    for(unsigned frame=0;frame<8000;frame++){
        if(frame%700==0){assert(fx_build(&p,&b));b.dynamics=(frame/700)%2;e.pending=b;e.generation++;}
        unsigned old_mode=e.latency_mode,old_on=e.latency_on;
        int32_t f[2]={200000,-100000};fx_process(&e,f,1,4096);
        if(old_mode!=e.latency_mode || old_on!=e.latency_on)assert(!e.bypass_level && !f[0] && !f[1]);
        assert(!e.faults && e.count<=FX_DELAY+1);
    }
    p.lim=0;assert(fx_build(&p,&b));b.dynamics=FX_DYNAMICS_LM;e.pending=b;e.generation++;
    for(unsigned i=0;i<1200;i++){int32_t f[2]={200000,-100000};fx_process(&e,f,1,4096);if(i==1199)assert(f[0]==200000 && f[1]==-100000);}
    assert(!e.latency_on && !e.detector_on && !e.count);
    printf("PASS: mode changes switch taps only at exact silence; dynamics bypass removes delay; fixed engine %zu B, LM detector %zu B\n",sizeof e,sizeof(lm_state_t));
}
static void input_history(void)
{
    lm_state_t seed;lm_reset(&seed);
    for(unsigned i=0;i<28677;i++)lm_gain(&seed,10,10);
    assert(seed.armed && seed.captured==10 && seed.long_env>9.99f);
    for(unsigned i=0;i<11030;i++)lm_gain(&seed,0,1);
    assert(seed.captured && !seed.long_hold && seed.long_env<9 && seed.low_age>=3*LM_LOOK);
    lm_state_t control=seed,ignored=seed,visible=seed,paused=seed;
    lm_gain(&control,0,1);lm_gain(&ignored,.009f*seed.history_peak,1);
    lm_gain(&visible,.012f*seed.history_peak,1);lm_gain(&paused,.018f*seed.history_peak,1);
    assert(!ignored.visible_hold && !ignored.pause && visible.visible_hold && !visible.pause);
    assert(paused.visible_hold && paused.pause);
    float frozen=visible.inverse;
    for(unsigned i=0;i<1324;i++){
        lm_gain(&control,0,1);lm_gain(&ignored,0,1);lm_gain(&visible,0,1);lm_gain(&paused,0,1);
        assert(control.inverse==ignored.inverse);
    }
    assert(visible.inverse==frozen && control.inverse<frozen && paused.long_env>control.long_env);
    assert(visible.long_env==control.long_env); /* Visible hold did not pause hidden state. */
    for(unsigned i=0;i<7000;i++){lm_gain(&control,0,1);lm_gain(&visible,0,1);lm_gain(&paused,0,1);}
    assert(fabsf(visible.inverse-control.inverse)<.001f && paused.inverse>control.inverse+.1f);
    puts("PASS: sustained input causally arms/captures the long tail; subthreshold input separates ignored, visible-only hold and hidden pause, then visible hold rejoins recovery");
}
static void preset_reset(void)
{
    fx_params_t p;fx_bank_t b;fx_engine_t e;setup(&p,&b,&e);
    for(unsigned i=0;i<40000;i++){
        int32_t f[2]={16000000,-8000000};fx_process(&e,f,1,4096);
    }
    for(unsigned i=0;i<LM_HOLD+200;i++){
        int32_t f[2]={10000,-5000};fx_process(&e,f,1,4096);
    }
    assert(e.detector.lm.captured>1 && e.gain<.8f);
    fx_engine_t retained=e;
    b.reset_lm=1;fx_publish(&e,&b);
    b.reset_lm=0;fx_publish(&e,&b); /* An edit before audio runs cannot cancel it. */
    assert(e.pending.reset_lm && e.detector.lm.captured==retained.detector.lm.captured);
    unsigned reset_at=0;
    for(unsigned i=0;i<2u*FX_BYPASS_FADE+20u;i++){
        float old=e.detector.lm.captured;
        int32_t f[2]={10000,-5000},g[2]={10000,-5000};
        fx_process(&e,f,1,4096);fx_process(&retained,g,1,4096);
        assert(!memcmp(e.delay,retained.delay,sizeof e.delay) && !memcmp(e.z,retained.z,sizeof e.z));
        assert(e.at==retained.at && e.time==retained.time && e.latency_mode==FX_DYNAMICS_LM && e.latency_on);
        if(old && !e.detector.lm.captured){
            assert(!reset_at && !e.bypass_level && !f[0] && !f[1]);reset_at=i+1;
            assert(!e.detector.lm.charge_s && !e.detector.lm.charge_f && !e.detector.lm.history_peak &&
                   !e.detector.lm.medium_hold && !e.detector.lm.long_hold && !e.detector.lm.visible_hold);
        }
        if(reset_at)assert(e.gain==1 && e.detector.lm.medium==1 && e.detector.lm.long_env==1);
        if(i>=2u*FX_BYPASS_FADE)assert(abs(f[0]-10000)<=1 && abs(f[1]+5000)<=1);
    }
    assert(reset_at==FX_BYPASS_FADE && !e.pending.reset_lm && !e.fade && e.bypass_level==FX_BYPASS_FADE);
    assert(retained.detector.lm.captured>1 && retained.gain<.8f); /* Ordinary edit retains history. */
    /* An already bypassed detector resets without changing dry audio/latency. */
    b.lim=0;fx_init(&e,&b);e.detector.lm=retained.detector.lm;
    b.reset_lm=1;fx_publish(&e,&b);
    for(unsigned i=0;i<FX_FADE+1u;i++){
        int32_t f[2]={10000,-5000};fx_process(&e,f,1,4096);
        assert(f[0]==10000 && f[1]==-5000 && !e.latency_on && e.bypass_level==FX_BYPASS_FADE);
        assert(!e.detector.lm.captured && !e.detector.lm.charge_s);
    }
    puts("PASS: pending LM reset survives coalescing, clears real long-release history exactly at 5-ms silence, 5-ms recovery, warm EQ/delay and bypass without a dip");
}
int main(void){setbuf(stdout,NULL);latency_and_ceiling();streaming_and_isolation();release_oracle();mode_switches();input_history();preset_reset();}
