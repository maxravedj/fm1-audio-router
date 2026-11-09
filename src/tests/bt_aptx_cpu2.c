/* SPDX-License-Identifier: MPL-2.0 */
/* Actual aptX transport, CPU2 queue and 24-bit FIR; no radio/hardware claims. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define APP_BT_RESAMPLE_CPU2
#define APP_BT_APTX
#define PLAT_TICKS_PER_US 24u
static unsigned core,irq,allocs,fail_alloc,jobs,locks;
static void (*worker)(uint32_t);static uint32_t arg,ticks;
static uint32_t plat_ticks24(void){return ++ticks;}
static uint32_t plat_lock(void){assert(!core);return locks++;}
static void plat_unlock(uint32_t f){assert(locks==f+1);locks=f;}
static void plat_cpu2_sync(void){__asm__ volatile("" ::: "memory");}
static int plat_cpu2_busy(void){return worker!=0;}
static int plat_cpu2_run(void (*fn)(uint32_t),uint32_t a){assert(!core);if(worker)return 0;worker=fn;arg=a;return 1;}
static int plat_cpu2_wait(void){assert(!irq && !core);if(worker){core=1;worker(arg);core=0;worker=0;jobs++;}return 0;}
static void *plat_work_alloc_top(uint32_t n){assert(!irq && !core);if(fail_alloc)return 0;void *m=malloc(n);assert(m);allocs++;return m;}
static void plat_work_free(void *p){assert(!irq && !core && !worker && allocs);free(p);allocs--;}
#include "../bt/br/bt_receiver_pcm.c"
#include "../bt/br/bt_receiver_aptx.c"
static void btr_decode_poll_irq(void){uint32_t f=plat_lock();if(btr_worker_enabled)btr_aptx_poll_inner();plat_unlock(f);}
static void reset(unsigned rate){btr_worker_stop();btr_aptx_reset();btr_audio_reset(rate);memset((void *)&btr_stats,0,sizeof btr_stats);}
static uint32_t seed=1;
static uint32_t rnd(void){seed=seed*1664525u+1013904223u;return seed;}
static void fir(void)
{
    static uint8_t low[BTR_RING][2];static int32_t expected[BTR_OUT_RING][2];
    for(unsigned rate=44100;rate<=48000;rate+=3900)for(unsigned trial=0;trial<64;trial++){
        reset(rate);btr_worker_enabled=0;btr_pcm_low=low;
        btr_r=UINT32_MAX-1015;btr_w=btr_r+1024;btr_out_r=btr_out_w=UINT32_MAX-511;
        btr_phase=rnd()&65535;btr_servo=(int32_t)(rnd()%33000)-16500;
        for(unsigned i=0;i<BTR_RING;i++)for(unsigned c=0;c<2;c++){btr_pcm[i][c]=(int16_t)rnd();low[i][c]=(uint8_t)(rnd()>>24);}
        memset(btr_output,0,sizeof btr_output);memset(expected,0,sizeof expected);
        uint32_t r=btr_r,ow=btr_out_w,ph=btr_phase,step=0,base=rate==48000?71332:65536;int32_t servo=btr_servo;
        const int16_t (*f)[96]=rate==48000?BTR_FIR_48:BTR_FIR_44;
        while(BTR_OUT_RING-(ow-btr_out_r)>=256 && btr_w-r>=377){
            int e=((int)(btr_w-r+(((ow-btr_out_r)*base)>>16))-3072)/16;if(e>66)e=66;if(e< -66)e= -66;
            servo+=(e*256-servo)/32;step=base+servo/256;
            for(unsigned i=0;i<256;i++){
                int64_t sum[2]={0,0};
                for(unsigned k=0;k<96;k++){
                    int c=f[ph>>10][k]+((f[(ph>>10)+1][k]-f[ph>>10][k])*(int)(ph&1023)+512)/1024;
                    for(unsigned ch=0;ch<2;ch++)sum[ch]+=((int64_t)btr_pcm[(r+k)&2047][ch]*256+low[(r+k)&2047][ch])*c;
                }
                for(unsigned ch=0;ch<2;ch++)expected[(ow+i)&2047][ch]=(int32_t)(sum[ch]>>15);
                ph+=step;r+=ph>>16;ph&=65535;
            }ow+=256;
        }
        core=1;btr_resample_fill();core=0;assert(!memcmp(expected,btr_output,sizeof expected) && btr_r==r && btr_phase==ph && btr_servo==servo);
        unsigned first=btr_out_r;
        for(unsigned gain=0;gain<=4096;gain+=1024){
            btr_out_r=first;btr_primed=1;int32_t out[2]={0};
            btr_pcm_mix(out,1,gain);
            for(unsigned ch=0;ch<2;ch++)assert(out[ch]==(int32_t)((int64_t)expected[first&2047][ch]*gain/4096));
        }
        btr_pcm_low=0;
    }
    /* Both 48-tap halves must split correctly at every possible wrap point.
     * Full-scale sign-aligned inputs also exercise the proven partial bounds. */
    for(unsigned rate=44100;rate<=48000;rate+=3900){
        const int16_t (*f)[96]=rate==48000?BTR_FIR_48:BTR_FIR_44;
        for(unsigned phase=0;phase<64;phase++)for(unsigned trial=0;trial<100;trial++){
            unsigned at=(2048-98+trial)&2047,frac=(trial*997u)&1023;
            int64_t sum[2]={0,0};int32_t actual[2];
            for(unsigned k=0;k<96;k++){
                int c=f[phase][k]+((f[phase+1][k]-f[phase][k])*(int)frac+512)/1024;
                unsigned p=(at+k)&2047;
                btr_pcm[p][0]=c<0?INT16_MIN:INT16_MAX;btr_pcm[p][1]=c<0?INT16_MAX:INT16_MIN;
                low[p][0]=(uint8_t)rnd();low[p][1]=(uint8_t)rnd();
                for(unsigned ch=0;ch<2;ch++)sum[ch]+=((int64_t)btr_pcm[p][ch]*256+low[p][ch])*c;
            }
            btr_fir24(btr_pcm,low,at,f[phase],frac,actual);
            for(unsigned ch=0;ch<2;ch++)assert(actual[ch]==(int32_t)(sum[ch]>>15));
        }
    }
    puts("PASS: 24-bit FIR, both rates/all phases, 12,800 full-scale/wrap cases vs int64 oracle");
    puts("PASS: 24-bit FIR vs independent int64 scalar oracle, 128 rate/phase/wrap/servo cases");
}
static FILE *output;
static void service(void)
{
    unsigned before=btr_w;
    if(worker){core=1;worker(arg);core=0;worker=0;jobs++;}
    if(output)for(unsigned i=before;i!=btr_w;i++)for(unsigned ch=0;ch<2;ch++){
        unsigned v=(uint16_t)btr_pcm[i&2047][ch];fputc(btr_pcm_low[i&2047][ch],output);fputc(v&255,output);fputc(v>>8,output);
    }
    btr_out_r=btr_out_w;irq=1;btr_decode_poll_irq();irq=0;
}
static void drain(void){unsigned guard=1000;while(worker || (btr_aptx && btr_aptx->pr!=btr_aptx->pw)){assert(guard--);service();}}
static uint8_t wire[1100];static unsigned seq=65530;
static void packet(const uint8_t *p,unsigned n,unsigned hd)
{
    if(!hd){memcpy(wire,p,n);btr_aptx_media(wire,n,0);}
    else {memset(wire,0,12);wire[0]=0x80;wire[1]=96;wire[2]=(uint8_t)(seq>>8);wire[3]=(uint8_t)seq++;wire[11]=1;memcpy(wire+12,p,n);btr_aptx_media(wire,n+12,1);}
    memset(wire,0xa5,sizeof wire);
}
int main(int argc,char **argv)
{
    assert(argc==5);fir();unsigned hd=(unsigned)atoi(argv[3]),rate=(unsigned)atoi(argv[4]);reset(rate);
    FILE *f=fopen(argv[1],"rb");assert(f);output=fopen(argv[2],"wb");assert(output);
    uint8_t p[900];unsigned n,chunk=(hd?6:4)*137;
    while((n=(unsigned)fread(p,1,chunk,f))){packet(p,n,hd);drain();}
    fclose(f);fclose(output);output=0;
    assert(jobs && allocs==1 && !btr_stats.malformed && !btr_stats.decoder_errors && !btr_stats.gaps && !btr_stats.overruns);
    /* Allocation failure, held in-flight queue, immutable ownership and drops. */
    reset(rate);fail_alloc=1;packet(p,hd?60:40,hd);assert(!worker && !allocs);fail_alloc=0;
    for(unsigned i=0;i<40;i++)packet(p,hd?600:400,hd);
    assert(worker && btr_stats.gaps && btr_aptx && allocs==1);
    uint8_t old[1008];struct btr_aptx_packet q=btr_aptx->packets[btr_aptx->pr&15];memcpy(old,btr_aptx->queue+(q.start&4095),q.bytes);
    packet(p,hd?600:400,hd);assert(!memcmp(old,btr_aptx->queue+(q.start&4095),q.bytes));
    reset(rate);assert(!worker && !allocs && !btr_pcm_low);
    /* No padding at wrap: all bytes of the fixed 4-KiB queue remain usable,
     * including an HD codeword split across its physical end. */
    packet(p,hd?6:4,hd);drain();
    btr_aptx->qr=btr_aptx->qw=4090;
    for(unsigned i=0;i<4;i++){
        packet(p,900,hd);
        assert(btr_aptx->pw-btr_aptx->pr==i+1);
        const struct btr_aptx_packet *q=&btr_aptx->packets[(btr_aptx->pw-1)&15];
        for(unsigned j=0;j<q->bytes;j++)assert(btr_aptx->queue[(q->start+j)&4095]==p[j]);
    }
    packet(p,hd?492:496,hd);assert(btr_aptx->qw-btr_aptx->qr==(hd?4092:4096));
    unsigned dropped=btr_stats.gaps;packet(p,hd?6:4,hd);assert(btr_stats.gaps==dropped+1);
    reset(rate);assert(!allocs && !worker);
    /* Malformed/truncated RTP and codewords never allocate. */
    for(unsigned i=0;i<10000;i++){
        unsigned bytes=i%sizeof wire;for(unsigned j=0;j<bytes;j++)wire[j]=(uint8_t)rnd();
        if(hd && bytes>=12)wire[0]=0;else if(!hd)bytes=4*(bytes/4)+1;
        btr_aptx_media(wire,bytes,hd);assert(!allocs && !worker);
    }
    puts("PASS: actual CPU2 decoder/queue, full 24-bit PCM, caller reuse, backpressure, malformed packets, teardown; no allocations or waits in IRQ/jobs");
}
