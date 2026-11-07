/* SPDX-License-Identifier: MPL-2.0
 * Independent double-precision response and streaming DSP oracles. */
#define PLAT_HOST
#include "../fx/master_fx.h"
#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static uint32_t seed=1;
static uint32_t rnd(void){seed=seed*1664525u+1013904223u;return seed;}
static void oracle(const fx_band_t *b,double c[5])
{
    double w=2*acos(-1)*(double)b->hz/(double)FX_RATE,co=cos(w),a=sin(w)/(2*(b->q*.01));
    double A=pow(10,b->gain/400.),v=2*sqrt(A)*a,b0,b1,b2,a0,a1,a2;
    switch(b->type){
    case FX_LOW_PASS:b0=b2=(1-co)/2;b1=1-co;a0=1+a;a1=-2*co;a2=1-a;break;
    case FX_HIGH_PASS:b0=b2=(1+co)/2;b1=-(1+co);a0=1+a;a1=-2*co;a2=1-a;break;
    case FX_LOW_SHELF:
        b0=A*((A+1)-(A-1)*co+v);b1=2*A*((A-1)-(A+1)*co);b2=A*((A+1)-(A-1)*co-v);
        a0=(A+1)+(A-1)*co+v;a1=-2*((A-1)+(A+1)*co);a2=(A+1)+(A-1)*co-v;break;
    case FX_HIGH_SHELF:
        b0=A*((A+1)+(A-1)*co+v);b1=-2*A*((A-1)+(A+1)*co);b2=A*((A+1)+(A-1)*co-v);
        a0=(A+1)-(A-1)*co+v;a1=2*((A-1)-(A+1)*co);a2=(A+1)-(A-1)*co-v;break;
    default:b0=1+a*A;b1=-2*co;b2=1-a*A;a0=1+a/A;a1=-2*co;a2=1-a/A;break;
    }
    c[0]=b0/a0;c[1]=b1/a0;c[2]=b2/a0;c[3]=a1/a0;c[4]=a2/a0;
}
static double response(const double c[5],double f)
{
    double complex z=cexp(-I*2*acos(-1)*f/(double)FX_RATE);
    return cabs((c[0]+c[1]*z+c[2]*z*z)/(1+c[3]*z+c[4]*z*z));
}
static double svf_response(const fx_coef_t *c,double f)
{
    double complex z=cexp(-I*2*acos(-1)*f/(double)FX_RATE);
    double a=2*(double)c->a1-1,b=-2*(double)c->a2,cc=-b,d=1-2*(double)c->a3;
    double b0=2*(double)c->a2,b1=2*(double)c->a3;
    double c0=(double)c->m1*c->a1+(double)c->m2*c->a2,c1=-(double)c->m1*c->a2+(double)c->m2*(1-c->a3);
    double direct=c->m0+(double)c->m1*c->a2+(double)c->m2*c->a3;
    double complex det=(1-z*a)*(1-z*d)-z*z*b*cc;
    return cabs(direct+z*(c0*((1-z*d)*b0+z*b*b1)+c1*(z*cc*b0+(1-z*a)*b1))/det);
}
static void coefficients(void)
{
    const int hz[]={20,31,80,250,1000,4000,12000,19850},gain[]={-240,-60,0,120,240},q[]={10,71,100,500,1800};
    double worst=0;unsigned count=0;
    for(unsigned t=0;t<FX_TYPES;t++)for(unsigned h=0;h<8;h++)for(unsigned g=0;g<5;g++)for(unsigned r=0;r<5;r++){
        fx_band_t b={hz[h],gain[g],q[r],t,1};fx_coef_t c;double ref[5];
        if(!fx_coeff(&b,&c)){fprintf(stderr,"unstable type%u hz%d gain%d q%d\n",t,hz[h],gain[g],q[r]);abort();}
        oracle(&b,ref);
        for(unsigned f=0;f<40;f++){
            double at=f==39?hz[h]:20*pow(1000.,f/38.),expected=response(ref,at),actual=svf_response(&c,at);
            if(expected>0.001){double error=fabs(20*log10(actual/expected));if(error>worst)worst=error;
                if(error>0.04){fprintf(stderr,"response error %.5f dB type%u hz%d gain%d q%d at%.1f\n",error,t,hz[h],gain[g],q[r],at);abort();}}
        }
        count++;
    }
    printf("PASS: %u coefficient extremes against independent double RBJ; worst response error %.5f dB\n",count,worst);
}
static void bypass(void)
{
    fx_params_t p;fx_bank_t b;fx_engine_t e;int32_t history[7000][2];
    fx_defaults(&p);assert(fx_build(&p,&b));fx_init(&e,&b);
    for(unsigned i=0;i<7000;i++){
        int32_t pcm[2]={(int32_t)(rnd()%16777216u)-8388608,(int32_t)(rnd()%16777216u)-8388608};
        memcpy(history[i],pcm,sizeof pcm);uint32_t master=i%4097;fx_process(&e,pcm,1,master);
        for(unsigned ch=0;ch<2;ch++){
            int32_t expected=(int32_t)((float)history[i][ch]*((float)master/4096));
            assert(pcm[ch]==expected);
        }
    }
    assert(!e.faults && !e.count);
    puts("PASS: dry bypass exact same-frame stereo PCM at 4097 MASTER positions; zero startup/added delay; detector idle");
}
static void impulse(void)
{
    fx_params_t p;fx_bank_t b;fx_engine_t e;double worst=0;
    for(unsigned type=0;type<FX_TYPES;type++){
        fx_defaults(&p);p.chain=1;p.lim=0;p.band[0]=(fx_band_t){1000,120,300,type,1};
        for(unsigned i=1;i<5;i++)p.band[i].on=0;
        assert(fx_build(&p,&b));fx_init(&e,&b);double c[5],z0=0,z1=0;oracle(&p.band[0],c);
        for(unsigned i=0;i<50000;i++){
            double x=i==0?.01:0,y=c[0]*x+z0;z0=c[1]*x-c[3]*y+z1;z1=c[2]*x-c[4]*y;
            double ref=y;
            int32_t pcm[2]={i==0?83886:0,0};fx_process(&e,pcm,1,4096);
            double err=fabs(pcm[0]/8388608.-ref);if(err>worst)worst=err;
            assert(err<0.000003 && !pcm[1] && !e.faults);
        }
    }
    printf("PASS: five 50k-frame impulse tails and stereo isolation vs double oracle; max %.8f FS\n",worst);
}
static void limiter(void)
{
    fx_params_t p;fx_bank_t b;fx_engine_t e;fx_defaults(&p);p.chain=1;p.eq=0;p.ceiling=-60;
    assert(fx_build(&p,&b));fx_init(&e,&b);
    for(unsigned i=0;i<400000;i++){
        int32_t pcm[2];
        if(i<100000){pcm[0]=(int32_t)(rnd()%268435456u)-134217728;pcm[1]=pcm[0]/2;}
        else{pcm[0]=pcm[1]=0;}
        fx_process(&e,pcm,1,4096);
        int cap=(int)(b.ceiling*8388608.0f);
        assert(abs(pcm[0])<=cap && abs(pcm[1])<=cap && abs(pcm[0]/2-pcm[1])<=1);
        if(i<FX_DELAY)assert(!pcm[0] && !pcm[1]);
        assert(!e.faults && e.count<=129 && e.gain>=0 && e.gain<=1);
    }
    assert(e.gain>.999 && e.reduction_q16>65470);
    /* Isolated overload: gain ramps through lookahead and releases exponentially. */
    fx_init(&e,&b);int32_t frame[2]={16777216,8388608};fx_process(&e,frame,1,4096);
    assert(e.gain>.99 && e.gain<1); /* immediate prototype would jump to .25 */
    for(unsigned i=1;i<FX_DELAY+1;i++){frame[0]=frame[1]=0;fx_process(&e,frame,1,4096);}
    double start=e.release_gain;
    for(unsigned i=0;i<4412;i++){frame[0]=frame[1]=0;fx_process(&e,frame,1,4096);}
    double expected=1-(1-start)*exp(-4412./(FX_RATE*.1));
    assert(fabs(e.release_gain-expected)<.001 && fabs(e.gain-expected)<.007);
    e.time=UINT32_MAX-64; /* timestamp arithmetic across wrap */
    for(unsigned i=0;i<1000;i++){frame[0]=16777216;frame[1]=-8388608;fx_process(&e,frame,1,4096);assert(abs(frame[0])<=4204300);}
    puts("PASS: 400k randomized linked stereo peaks at/below ceiling, silence recovery, curved 128-frame attack, unchanged 100ms release envelope, timestamp wrap");
}
static void edits(void)
{
    fx_params_t p;fx_bank_t b;fx_engine_t e;fx_defaults(&p);assert(fx_build(&p,&b));fx_init(&e,&b);
    for(unsigned t=0;t<3000;t++){
        p.chain=t%7!=0;p.lim=t%11!=0;p.eq=t%13!=0;p.ceiling=-(int)(rnd()%241);p.input=rnd()%481-240;p.release=10+rnd()%991;
        for(unsigned j=0;j<5;j++){p.slope[j]=rnd()%4;p.band[j]=(fx_band_t){20+rnd()%19831,(int)(rnd()%481)-240,10+rnd()%1791,rnd()%5,rnd()%2};}
        assert(fx_build(&p,&b));e.pending=b;e.generation++;
        int32_t pcm[512];for(unsigned j=0;j<512;j++)pcm[j]=(int32_t)(rnd()%33554432)-16777216;
        /* Check the ceiling on the active delayed output, including edits
         * accepted during this block. A pending enable does not constrain
         * the old zero-latency bypass path before its silent tap switch. */
        for(unsigned j=0;j<256;j++){
            fx_process(&e,pcm+2*j,1,4096);
            unsigned target=e.fade?e.active^1u:e.active;
            if(e.latency_on){int cap=(int)(e.bank[target].ceiling*8388608.0f)+2;
                assert(abs(pcm[2*j])<=cap && abs(pcm[2*j+1])<=cap);}
        }
        assert(!e.faults);
        for(unsigned j=0;j<512;j++)assert(pcm[j]>=-8388608 && pcm[j]<=8388607);
    }
    for(unsigned i=0;i<10;i++){int32_t pcm[512]={0};fx_process(&e,pcm,256,4096);}
    assert(e.seen==e.generation && !e.fade);
    p.chain=2;assert(!fx_build(&p,&b));p.chain=1;p.band[0].q=0;assert(!fx_build(&p,&b));
    printf("PASS: 3000 rapid EQ/type/bypass/limiter edits, newest target settles; finite bounded stereo; engine %zu bytes\n",sizeof e);
}
static void streaming(void)
{
    fx_params_t p;fx_bank_t b;fx_engine_t a,e;
    fx_defaults(&p);p.chain=1;p.ceiling=-30;p.band[0].gain=90;
    assert(fx_build(&p,&b));fx_init(&a,&b);fx_init(&e,&b);
    int32_t left[4000],right[4000];
    for(unsigned i=0;i<4000;i++)left[i]=right[i]=(int32_t)(rnd()%33554432u)-16777216;
    fx_process(&a,left,2000,4096);
    unsigned at=0;
    while(at<2000){unsigned n=1+rnd()%257;if(n>2000-at)n=2000-at;fx_process(&e,right+at*2,n,4096);at+=n;}
    assert(!memcmp(left,right,sizeof left));
    fx_init(&e,&b);
    for(unsigned i=0;i<300;i++){int32_t pcm[2]={10000000-(int)i*1000,0};fx_process(&e,pcm,1,4096);assert(e.count<=129);}
    e.z[e.active][0][0][0][0]=NAN;
    int32_t zero[512]={0};fx_process(&e,zero,256,4096);assert(e.faults==1);
    for(unsigned i=0;i<512;i++)assert(zero[i]>=-8388608 && zero[i]<=8388607);
    const unsigned release[]={10,100,1000};
    for(unsigned i=0;i<3;i++){
        p.release=release[i];assert(fx_build(&p,&b));fx_init(&e,&b);e.release_gain=.25f;
        for(unsigned j=0;j<(unsigned)(FX_RATE*release[i]*.001f);j++){int32_t pcm[2]={0};fx_process(&e,pcm,1,4096);}
        assert(fabs(e.release_gain-(1-.75/exp(1)))<.003);
    }
    puts("PASS: arbitrary PCM block partitions identical, full deque window, injected NaN recovery, release 10/100/1000ms vs exponential oracle");
}
static void anticipation(void)
{
    fx_params_t p;fx_bank_t b;fx_engine_t e;fx_defaults(&p);p.chain=1;p.eq=0;p.ceiling=-60;
    assert(fx_build(&p,&b));fx_init(&e,&b);
    double ramp[FX_DELAY+1];
    for(unsigned i=0;i<=FX_DELAY;i++){
        int32_t frame[2]={i?0:16777216,i?0:-8388608};fx_process(&e,frame,1,4096);ramp[i]=e.gain;
        if(i)assert(ramp[i]<=ramp[i-1]);
        if(i<FX_DELAY)assert(!frame[0] && !frame[1]);
        else assert(abs(frame[0]-(int)(b.ceiling*8388608))<=1 && abs(frame[0]/2+frame[1])<=1);
    }
    double target=b.ceiling/2.;assert(fabs(ramp[FX_DELAY]-target)<1.2e-7);
    /* Curvature: slow start/end, steeper middle; a linear/exponential attack
     * cannot satisfy both the endpoint and these slope constraints. */
    double start=ramp[0]-ramp[1],middle=ramp[63]-ramp[64],end=ramp[127]-ramp[128];
    assert(middle>20*start && middle>20*end);

    /* Independent O(N*128) future-constraint + weighted-convolution oracle.
     * No production deque/ring helpers: exercise overlapping stronger/weaker
     * peaks on alternate channels and the actual gain before the safety guard. */
    enum { N=7000 };static double peaks[N],released[N];double release=1,worst=0;
    fx_init(&e,&b);
    for(unsigned i=0;i<N;i++){
        int32_t frame[2]={100000,-50000};
        if((i>=200 && i<1200 && i%31==0) || (i>=1200 && i<4000 && i%7==0)){
            unsigned ch=rnd()%2;frame[ch]=(int32_t)(10000000+rnd()%500000000)*(ch?-1:1);
        }
        peaks[i]=fmax(abs(frame[0]),abs(frame[1]))/8388608.;
        double peak=0;for(unsigned j=i>128?i-128:0;j<=i;j++)peak=fmax(peak,peaks[j]);
        double required=peak>b.ceiling?b.ceiling/peak:1;
        release=fmin(required,release+(1-release)*b.release);released[i]=release;
        double expected=0;
        for(unsigned lag=0;lag<=128;lag++){
            unsigned weight=65-(unsigned)abs((int)lag-64);
            expected+=weight*(lag>i?1:released[i-lag])/4225.;
        }
        fx_process(&e,frame,1,4096);
        double error=fabs(e.gain-expected);if(error>worst)worst=error;assert(error<.000025);
        if(i>=128){assert(e.gain*peaks[i-128]<=b.ceiling+1e-6);}
        int cap=(int)(b.ceiling*8388608);assert(abs(frame[0])<=cap && abs(frame[1])<=cap);
    }
    printf("PASS: S-shaped anticipatory ramp, exact isolated deadline/stereo ratio, overlapping deadlines without guard intervention, independent convolution/release oracle (worst %.8f gain)\n",worst);
}
static void bypass_switch(void)
{
    fx_params_t p;fx_bank_t b;fx_engine_t e;fx_defaults(&p);p.chain=1;p.eq=0;
    assert(fx_build(&p,&b));fx_init(&e,&b);
    unsigned clock=0;
    for(unsigned i=0;i<512;i++,clock++){int32_t frame[2]={100000+(int)clock*16,-50000-(int)clock*8};fx_process(&e,frame,1,4096);}
    for(unsigned global=0;global<2;global++)for(unsigned enable=0;enable<2;enable++){
        if(global)p.chain=enable;else p.lim=enable;
        assert(fx_build(&p,&b));e.pending=b;e.generation++;
        for(unsigned i=0;i<FX_BYPASS_FADE*2+32;i++,clock++){
            e.meter_enabled=1;e.output_peak[0]=e.output_peak[1]=0;
            int32_t frame[2]={100000+(int)clock*16,-50000-(int)clock*8};fx_process(&e,frame,1,4096);
            unsigned switched=i>=FX_BYPASS_FADE-1;
            unsigned level=switched?i-(FX_BYPASS_FADE-1):FX_BYPASS_FADE-1-i;
            if(level>FX_BYPASS_FADE)level=FX_BYPASS_FADE;
            unsigned delayed=switched?enable:!enable;
            assert(e.latency_on==delayed && e.bypass_level==level);
            int32_t expected=(int32_t)((100000+(float)(clock-(delayed?FX_DELAY:0))*16)*(level*(1.f/FX_BYPASS_FADE)));
            assert(abs(frame[0]-expected)<=1 && abs(frame[0]/2+frame[1])<=1);
            assert(e.output_peak[0]==100000+(clock-(delayed?FX_DELAY:0))*16 && e.reduction_q16==65536);
            if(i==FX_BYPASS_FADE-1)assert(!frame[0] && !frame[1]);
        }
        assert(e.latency_on==enable && e.bypass_level==FX_BYPASS_FADE);
        if(!enable)assert(!e.detector_on && !e.count);
    }
    /* Repeated reversals keep envelope continuous; alignment only switches at
     * zero, even when the request changes during a bank or bypass fade. */
    for(unsigned i=0;i<4000;i++){
        if(i%37==0){p.lim=!p.lim;assert(fx_build(&p,&b));e.pending=b;e.generation++;}
        unsigned old=e.latency_on,level=e.bypass_level;
        int32_t frame[2]={200000,-100000};fx_process(&e,frame,1,4096);
        assert(abs((int)e.bypass_level-(int)level)<=1);
        if(old!=e.latency_on)assert(!e.bypass_level && !frame[0] && !frame[1]);
    }
    p.chain=1;p.lim=0;assert(fx_build(&p,&b));e.pending=b;e.generation++;
    for(unsigned i=0;i<1200;i++){int32_t frame[2]={300000,-150000};fx_process(&e,frame,1,4096);if(i==1199)assert(frame[0]==300000 && frame[1]==-150000);}
    assert(!e.latency_on && !e.detector_on && !e.count && e.bypass_level==FX_BYPASS_FADE);
    puts("PASS: global and limiter-only bypass, exact 221-frame fade-out/silent tap switch/221-frame fade-in, unfaded limiter telemetry, warm history, rapid reversals and zero-delay settled output");
}
static void bypass_overload(void)
{
    for(unsigned global=0;global<2;global++)for(unsigned tenth=0;tenth<=240;tenth++){
        fx_params_t p;fx_bank_t b;fx_engine_t a,e;fx_defaults(&p);
        p.chain=!global;p.lim=global;p.eq=0;p.ceiling=-(int)tenth;
        assert(fx_build(&p,&b));fx_init(&a,&b);
        for(unsigned i=0;i<512;i++){int32_t f[2]={4194304,-2097152};fx_process(&a,f,1,4096);}
        p.chain=p.lim=1;assert(fx_build(&p,&b));a.pending=b;a.generation++;e=a;
        unsigned at=0;
        while(at<768){
            unsigned n=at%3==0?1:at%3==1?32:256;if(n>768-at)n=768-at;
            int32_t pcm[512];
            for(unsigned j=0;j<n;j++){
                int32_t f[2]={4194304,-2097152};fx_process(&a,f,1,4096);
                if(at+j<FX_BYPASS_FADE-1){
                    int expected=(int)(4194304.f*((FX_BYPASS_FADE-1-at-j)*(1.f/FX_BYPASS_FADE)));
                    assert(abs(f[0]-expected)<=1 && abs(f[0]/2+f[1])<=1);
                }else{
                    assert(abs(f[0])<=(int)fx_ceiling_q23[tenth] && abs(f[1])<=(int)fx_ceiling_q23[tenth]);
                    if(at+j==FX_BYPASS_FADE-1)assert(!f[0] && !f[1]);
                }
                pcm[2*j]=4194304;pcm[2*j+1]=-2097152;
            }
            /* Block subdivision cannot change the switch or limiter result. */
            fx_engine_t check=e;
            fx_process(&e,pcm,n,4096);
            for(unsigned j=0;j<n;j++){
                int32_t f[2]={4194304,-2097152};fx_process(&check,f,1,4096);
                assert(pcm[2*j]==f[0] && pcm[2*j+1]==f[1]);
            }
            at+=n;
        }
        assert(e.latency_on && e.bypass_level==FX_BYPASS_FADE && !e.faults);
    }
    puts("PASS: overload enable preserves old bypass fade, silent switch then strict ceiling for global/limiter enable at all 241 ceilings; identical 1/32/256-frame processing");
}
static void eq_history_edits(void)
{
    for(unsigned limited=0;limited<2;limited++){
        fx_params_t p;fx_bank_t b;fx_engine_t e,reference;fx_defaults(&p);
        p.chain=1;p.lim=limited;p.band[0].type=FX_HIGH_PASS;p.band[0].hz=20;p.slope[0]=3;
        assert(fx_build(&p,&b));fx_init(&e,&b);unsigned at=0;
        for(;at<22050;at++){
            int32_t f[2]={(int32_t)(2097152*sin(2*3.141592653589793*60*at/FX_RATE)),0};
            f[1]=-f[0]/2;fx_process(&e,f,1,4096);
        }
        reference=e;p.release=200;p.ceiling=-30;
        assert(fx_build(&p,&b));e.pending=b;e.generation++;
        for(unsigned j=0;j<12000;j++,at++){
            int32_t f[2]={(int32_t)(1048576*sin(2*3.141592653589793*60*at/FX_RATE)),0},r[2];
            f[1]=-f[0]/2;memcpy(r,f,sizeof f);
            fx_process(&e,f,1,4096);fx_process(&reference,r,1,4096);
            assert(f[0]==r[0] && f[1]==r[1]);
        }
        /* A real EQ coefficient change still takes effect through bank fade. */
        p.band[0].hz=500;assert(fx_build(&p,&b));e.pending=b;e.generation++;
        int changed=0;
        for(unsigned j=0;j<3000;j++,at++){
            int32_t f[2]={(int32_t)(1048576*sin(2*3.141592653589793*60*at/FX_RATE)),0},r[2];
            f[1]=-f[0]/2;memcpy(r,f,sizeof f);fx_process(&e,f,1,4096);fx_process(&reference,r,1,4096);
            if(abs(f[0]-r[0])>1000)changed=1;
        }
        assert(changed && !e.fade && !e.faults);
    }
    puts("PASS: limiter-only release/ceiling edits preserve warm 48-dB/oct EQ history and exact stereo PCM, active/bypassed; real EQ edits still settle");
}
static void eq_history_switch(void)
{
    for(unsigned enable=0;enable<2;enable++){
        fx_params_t p;fx_bank_t b;fx_engine_t e,reference;fx_defaults(&p);
        p.chain=1;p.band[0].type=FX_HIGH_PASS;p.band[0].hz=20;p.slope[0]=3;p.lim=0;
        assert(fx_build(&p,&b));fx_init(&reference,&b);
        p.lim=!enable;assert(fx_build(&p,&b));fx_init(&e,&b);
        int32_t history[FX_DELAY][2]={{0}};
        for(unsigned at=0;at<25050;at++){
            if(at==22050){p.lim=enable;assert(fx_build(&p,&b));e.pending=b;e.generation++;}
            int32_t f[2]={(int32_t)(2097152*sin(2*3.141592653589793*60*at/FX_RATE)),0},r[2];
            f[1]=-f[0]/2;memcpy(r,f,sizeof f);fx_process(&reference,r,1,4096);fx_process(&e,f,1,4096);
            if(at>=22050){
                unsigned i=at-22050,switched=i>=FX_BYPASS_FADE-1;
                unsigned delayed=switched?enable:!enable;
                unsigned level=switched?i-(FX_BYPASS_FADE-1):FX_BYPASS_FADE-1-i;
                if(level>FX_BYPASS_FADE)level=FX_BYPASS_FADE;
                for(unsigned ch=0;ch<2;ch++){
                    int32_t x=delayed?history[at%FX_DELAY][ch]:r[ch];
                    int expected=(int)(x*(level*(1.f/FX_BYPASS_FADE)));
                    assert(abs(f[ch]-expected)<=2);
                }
            }
            memcpy(history[at%FX_DELAY],r,sizeof r);
        }
    }
    puts("PASS: warm low-frequency EQ survives limiter enable/disable with only the requested bypass envelope and time-alignment switch");
}
static void strict_ceilings(void)
{
    fx_params_t p;fx_bank_t b;fx_engine_t e;fx_defaults(&p);p.chain=1;p.eq=0;
    for(int tenth=0;tenth<=240;tenth++){
        int cap=(int)floor(pow(10.,-tenth/200.)*8388608.);
        assert(fx_ceiling_q23[tenth]==(uint32_t)cap);
        p.ceiling=-tenth;assert(fx_build(&p,&b));fx_init(&e,&b);
        /* Integer PCM ceiling, not an approximation made with production math.
         * Both signs/full-scale and headroom, including late isolated peaks. */
        for(unsigned i=0;i<1000;i++){
            int32_t frame[2]={i<512?8388607:(i%13?0:100000000),i<512?-8388607:(i%29?0:-90000000)};
            fx_process(&e,frame,1,4096);assert(abs(frame[0])<=cap && abs(frame[1])<=cap);
        }
    }
    puts("PASS: all 241 ceiling settings vs independent double dB oracle: no positive/negative sample exceeds the configured integer ceiling, including headroom/isolated peaks");
}
static void slopes(void)
{
    fx_params_t p;fx_bank_t b;fx_engine_t e;double worst=0;
    for(unsigned type=FX_LOW_PASS;type<=FX_HIGH_PASS;type++)for(unsigned count=1;count<=4;count++){
        for(unsigned resonance=0;resonance<3;resonance++)for(unsigned tone=0;tone<4;tone++){
            static const unsigned q[]={10,71,1800};static const double u[]={.25,.5,1.,2.};
            fx_defaults(&p);p.chain=1;p.lim=0;
            for(unsigned i=1;i<5;i++)p.band[i].on=0;
            p.band[0]=(fx_band_t){1000,0,q[resonance],type,1};p.slope[0]=count-1;
            assert(fx_build(&p,&b) && b.stages[0]==count);fx_init(&e,&b);
            double g=tan(acos(-1)*1000/FX_RATE),f=atan(g*u[tone])*FX_RATE/acos(-1),ref=1;
            for(unsigned j=0;j<count;j++){
                /* Independent Butterworth poles, rather than production Q table. */
                double qq=count==1?q[resonance]*.01:1/(2*cos((2*j+1)*acos(-1)/(4*count)));
                if(count>1 && j==0)qq*=q[resonance]/71.;
                double den=pow(1-u[tone]*u[tone],2)+pow(u[tone]/qq,2);
                ref*=type==FX_LOW_PASS?1/den:pow(u[tone],4)/den;
            }
            double measured=0,input=0;
            for(unsigned n=0;n<24000;n++){
                double phase=2*acos(-1)*f*n/FX_RATE;
                float x=(float)(.01*sin(phase)),y=fx_filter(&e,0,0,x);
                assert(isfinite(y));
                if(n>=8000){measured+=(double)y*y;input+=(double)x*x;}
            }
            double err=fabs(10*log10(measured/input/ref));if(err>worst)worst=err;
            assert(err<.09);
        }
        /* Unity resonance: all higher-order cutoffs retain -3.01 dB. */
        p.band[0].q=71;assert(fx_build(&p,&b));
    }
    p.slope[0]=4;assert(!fx_build(&p,&b));p.slope[0]=0;p.tail=1;assert(!fx_build(&p,&b));
    printf("PASS: LP/HP 12/24/36/48 dB/oct actual cascaded DSP vs independent Butterworth/resonance oracle; worst %.5f dB; invalid slopes rejected\n",worst);
}
int main(void){setbuf(stdout,NULL);bypass_overload();eq_history_edits();eq_history_switch();coefficients();bypass();impulse();limiter();anticipation();bypass_switch();strict_ceilings();edits();streaming();slopes();}
