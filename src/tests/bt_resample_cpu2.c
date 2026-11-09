/* SPDX-License-Identifier: MPL-2.0 */
/* Real production pipeline, threaded jobs, scalar FIR oracle and tap ordering. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#define PLAT_HOST_MEMORY
#define APP_DYNAMIC_DISPLAY
#define PLAT_HOST
#include "../include/plat.h"
#define plat_work_alloc unchecked_alloc
#define plat_work_alloc_top unchecked_alloc_top
#define plat_work_free unchecked_free
#include "../device/work_memory.h"
#undef plat_work_alloc
#undef plat_work_alloc_top
#undef plat_work_free
#undef PLAT_HOST
#define APP_BT_AAC
#define APP_BT_APTX
#define APP_BT_SBC_CPU2
#define APP_BT_RESAMPLE_CPU2
#define APP_AUDIO_MONITOR
#define APP_INPUT_FADERS
#define APP_USB_OUT_ROUTING
static __thread unsigned core;
static unsigned thread_valid,waits,irq,locks,jobs;
void *plat_work_alloc(uint32_t bytes){assert(!irq && !core);return unchecked_alloc(bytes);}
void *plat_work_alloc_top(uint32_t bytes){assert(!irq && !core);return unchecked_alloc_top(bytes);}
void plat_work_free(void *p){assert(!irq && !core);unchecked_free(p);}
static uint32_t job_arg,ticks;
static void (*job_fn)(uint32_t);
static pthread_t thread;
static int busy,hold_job;
uint32_t plat_ticks24(void){return __atomic_add_fetch(&ticks,24,__ATOMIC_RELAXED);}
uint32_t plat_lock(void){assert(!core);return locks++;}
void plat_unlock(uint32_t f){assert(locks==f+1);locks=f;}
void plat_cpu2_sync(void){__atomic_thread_fence(__ATOMIC_SEQ_CST);}
int plat_cpu2_busy(void){return __atomic_load_n(&busy,__ATOMIC_ACQUIRE);}
static void *work(void *unused){(void)unused;core=1;while(__atomic_load_n(&hold_job,__ATOMIC_ACQUIRE))sched_yield();job_fn(job_arg);__atomic_store_n(&busy,0,__ATOMIC_RELEASE);return 0;}
static void finish(void){if(thread_valid){assert(!pthread_join(thread,0));thread_valid=0;}}
int plat_cpu2_run(void (*fn)(uint32_t),uint32_t arg)
{
    assert(!core);if(plat_cpu2_busy())return 0;finish();job_fn=fn;job_arg=arg;
    __atomic_store_n(&busy,1,__ATOMIC_RELEASE);assert(!pthread_create(&thread,0,work,0));thread_valid=1;jobs++;return 1;
}
int plat_cpu2_wait(void){assert(!irq && !core);waits++;finish();return 0;}
static int32_t monitored[512],recorded[512];
static unsigned monitor_at,record_mode,meter_on;
static uint32_t meter_peak[2];
static float fader=1;
void plat_audio_monitor_reset(int source){(void)source;assert(!core);}
void plat_audio_monitor_bt_begin(void){assert(!core);monitor_at=0;}
void plat_audio_monitor_bt_sample(int32_t l,int32_t r){assert(!core && monitor_at<256);monitored[monitor_at*2]=l;monitored[monitor_at*2+1]=r;monitor_at++;}
static unsigned app_usb_out_bt_mode(void){assert(!core);return record_mode;}
static void app_usb_out_bt_sample(uint32_t i,uint32_t ch,int32_t v){assert(!core && i<256);recorded[2*i+ch]=v;}
static int app_input_fader_meter_enabled(void){assert(!core);return meter_on;}
static float app_input_fader_gain(unsigned ch){assert(!core && ch==1);return fader;}
static void app_input_fader_meter_peaks(unsigned ch,uint32_t l,uint32_t r){assert(!core && ch==1);meter_peak[0]=l;meter_peak[1]=r;}
#include "../bt/br/bt_receiver_pcm.c"
static FILE *decoded;
static unsigned decoded_frames;
static void checked_put(const int16_t *p,uint32_t n)
{
    assert(core && n<=btr_pcm_space());decoded_frames+=n;
    if(decoded)assert(fwrite(p,4,n,decoded)==n);
    btr_pcm_put(p,n);
}
#define btr_pcm_put checked_put
#include "../bt/br/bt_receiver_media.c"
#undef btr_pcm_put
static uint8_t ba_live;
#define BTR_SECURE() 1
#define BTR_SEND(p,n) ((void)(p),(void)(n))
#define BTR_FLUSH(rate) do {btr_media_reset();btr_audio_reset(rate);} while(0)
#include "../bt/br/bt_receiver_l2cap.c"
static uint32_t rng=237;
static uint32_t random32(void){return rng=rng*1664525u+1013904223u;}
static void reset(unsigned rate){btr_media_reset();btr_audio_reset(rate);}
static void oracle(void)
{
    static int32_t want[BTR_OUT_RING][2];
    for(unsigned rate=44100;rate<=48000;rate+=3900)for(unsigned phase=0;phase<64;phase++)for(unsigned frac=0;frac<4;frac++) {
        reset(rate);btr_worker_enabled=0;
        btr_r=UINT32_MAX-1015u;btr_w=btr_r+1024;
        btr_out_r=btr_out_w=UINT32_MAX-511u;
        btr_phase=phase*1024+(uint32_t[]){0,1,511,1023}[frac];btr_servo=(int32_t)(random32()%33000)-16500;
        memset(btr_output,0,sizeof btr_output);memset(want,0,sizeof want);
        for(unsigned i=0;i<BTR_RING;i++)for(unsigned c=0;c<2;c++)btr_pcm[i][c]=(int16_t)random32();
        uint32_t r=btr_r,ow=btr_out_w,ph=btr_phase,step=0,base=rate==48000?71332:65536;
        int32_t servo=btr_servo;
        const int16_t (*f)[96]=rate==48000?BTR_FIR_48:BTR_FIR_44;
        while(BTR_OUT_RING-(ow-btr_out_r)>=256 && btr_w-r>=377) {
            int32_t error=((int32_t)(btr_w-r+(((ow-btr_out_r)*base)>>16))-(int32_t)BTR_TARGET)/16;
            if(error>66)error=66;if(error< -66)error= -66;
            servo+=(error*256-servo)/32;step=base+servo/256;
            for(unsigned i=0;i<256;i++) {
                int64_t sum[2]={0,0};
                for(unsigned k=0;k<96;k++) {
                    int32_t c=f[ph>>10][k]+((f[(ph>>10)+1][k]-f[ph>>10][k])*(int32_t)(ph&1023)+512)/1024;
                    for(unsigned ch=0;ch<2;ch++)sum[ch]+=(int32_t)btr_pcm[(r+k)&(BTR_RING-1)][ch]*c;
                }
                for(unsigned ch=0;ch<2;ch++)want[(ow+i)&(BTR_OUT_RING-1)][ch]=(int32_t)(sum[ch]>>15);
                ph+=step;r+=ph>>16;ph&=65535;
            }
            ow+=256;
        }
        core=1;btr_resample_fill();core=0;
        assert(!memcmp(want,btr_output,sizeof want));assert(btr_r==r && btr_out_w==ow && btr_phase==ph && btr_servo==servo && btr_step==step);
        /* CPU0 only applies current gain/trim; raw tap precedes both. */
        for(unsigned mode=1;mode<=2;mode++)for(unsigned g=0;g<=4096;g+=1024) {
            btr_out_r=UINT32_MAX-511u;btr_primed=1;record_mode=mode;fader=.5f;meter_on=1;
            int32_t audio[512]={0};uint32_t start=btr_out_r;
            irq=1;btr_pcm_mix(audio,256,g);irq=0;
            for(unsigned i=0;i<256;i++)for(unsigned ch=0;ch<2;ch++) {
                int32_t v=want[(start+i)&(BTR_OUT_RING-1)][ch];
                int32_t raw=(int32_t)((int64_t)v*256*g/4096),post=(int32_t)((float)raw*.5f);
                assert(monitored[2*i+ch]==v*256 && audio[2*i+ch]==post && recorded[2*i+ch]==(mode==1?raw:post));
            }
        }
    }
    fader=1;record_mode=meter_on=0;
    puts("PASS: actual CPU2 96-tap FIR vs independent int64/division oracle, 512 phase/rate cases, stereo, wrap, servo and CPU0 pre/post-trim taps");
}
static void pump(void)
{
    for(unsigned i=0;i<80;i++) {
        finish();btr_worker_check();btr_sbc_poll();btr_aac_poll();btr_resample_poll();
        if(!plat_cpu2_busy())break;
    }
    finish();
}
static unsigned seq,heard;
static void consume(void)
{
    int32_t out[512]={0};unsigned before=waits;irq=1;btr_pcm_mix(out,256,4096);irq=0;assert(waits==before);
    for(unsigned i=0;i<512;i++)heard|=out[i]!=0;
}
static void sbc_stream(const char *name,const char *out)
{
    reset(44100);FILE *f=fopen(name,"rb");assert(f);uint8_t stream[65536];unsigned n=fread(stream,1,sizeof stream,f);fclose(f);
    uint32_t rate=(stream[1]&0xc0)==0xc0?48000:44100;reset(rate);
    unsigned before=decoded_frames;decoded=fopen(out,"wb");assert(decoded);heard=0;
    for(unsigned at=0;at<n;) {
        unsigned len=0,frames=0;
        while(frames<15 && at+len<n) {
            unsigned bytes=btr_frame_size(stream+at+len,n-at-len,BTR_CAPS);assert(bytes);
            if(len+bytes>990)break;len+=bytes;frames++;
        }
        assert(frames);
        uint8_t packet[1200]={0x80,96};packet[2]=seq>>8;packet[3]=seq++;packet[12]=(uint8_t)frames;memcpy(packet+13,stream+at,len);
        btr_media(packet,len+13,BTR_CAPS);memset(packet,0xa5,sizeof packet);at+=len;pump();
        while(btr_out_w-btr_out_r>1536){consume();pump();}
    }
    for(unsigned i=0;i<80 && btr_sbc && btr_sbc->packet_r!=btr_sbc->packet_w;i++){consume();pump();}
    fclose(decoded);decoded=0;assert(heard && decoded_frames>before && !btr_stats.overruns);
    /* Teardown while an actual resampling job is outstanding must join it. */
    btr_worker_run(btr_resample_job,0);reset(rate);assert(!thread_valid && !btr_sbc && !btr_aac && !btr_out_w);
    puts("PASS: threaded SBC decode+FIR, immutable input, nonblocking audio, backpressure and resample join-before-reset");
}
static void aac_stream(const char *name,const char *out,int unmarked)
{
    reset(44100);FILE *f=fopen(name,"rb");assert(f);uint8_t conf[6],packet[1200];assert(fread(conf,1,6,f)==6);
    reset(conf[2]&0x80?48000:44100);decoded=fopen(out,"wb");assert(decoded);heard=0;
    for(;;) {
        uint8_t size[4];assert(fread(size,1,4,f)==4);unsigned n=(unsigned)size[0]|(unsigned)size[1]<<8|(unsigned)size[2]<<16|(unsigned)size[3]<<24;
        if(!n)break;assert(n<=sizeof packet && fread(packet,1,n,f)==n);if(unmarked)packet[1]&=127;
        btr_aac_media(packet,n,conf);memset(packet,0xa5,sizeof packet);pump();
        while(btr_out_w-btr_out_r>1536){consume();pump();}
    }
    if(unmarked && btr_aac->assembling)btr_aac_queue(conf,1);
    for(unsigned i=0;i<200 && btr_aac->packet_r!=btr_aac->packet_w;i++){consume();pump();}
    fclose(f);fclose(decoded);decoded=0;
    assert(heard && btr_aac->packet_r==btr_aac->packet_w && !btr_stats.overruns);
    reset(44100);assert(!btr_aac && !btr_sbc && !thread_valid);
    puts("PASS: threaded AAC decode+FIR, marked/unmarked RTP, complete queue drain and join-before-free");
}
static void putbits(uint8_t *p,unsigned *at,uint32_t value,unsigned n)
{while(n--){if((value>>n)&1)p[*at/8]|=(uint8_t)(1u<<(7-*at%8));(*at)++;}}
static void multi_au(const char *name)
{
    reset(44100);btr_worker_enabled=0;FILE *f=fopen(name,"rb");assert(f);uint8_t conf[6],packet[1200];assert(fread(conf,1,6,f)==6);
    while(!btr_aac || !btr_aac->packet_w) {
        uint8_t z[4];assert(fread(z,1,4,f)==4);unsigned n=z[0]|z[1]<<8|z[2]<<16|z[3]<<24;
        assert(n && n<=sizeof packet && fread(packet,1,n,f)==n);btr_aac_media(packet,n,conf);
    }
    fclose(f);struct btr_aac_mem *m=btr_aac;struct btr_aac_packet q=m->packets[0];
    const uint8_t *mux=m->queue+(q.start&(BTR_AAC_QUEUE_BYTES-1));
    core=1;assert(btr_aac_element(conf,mux,q.bytes));core=0;
    unsigned offset=m->offsets[0],len=m->lengths[0],prefix=offset-(len/255+1)*8;
    uint8_t multi[8192]={0};unsigned at=0;
    for(unsigned k=0;k<prefix;k++)putbits(multi,&at,(mux[k/8]>>(7-k%8))&1,1);
    for(unsigned k=0;k<6;k++){unsigned bit=3+k;multi[bit/8]&=(uint8_t)~(1u<<(7-bit%8));if((2u>>(5-k))&1)multi[bit/8]|=1u<<(7-bit%8);}
    for(unsigned au=0;au<3;au++) {
        for(unsigned n=len;n>=255;n-=255)putbits(multi,&at,255,8);
        putbits(multi,&at,len%255,8);
        for(unsigned k=0;k<len*8;k++)putbits(multi,&at,(mux[(offset+k)/8]>>(7-(offset+k)%8))&1,1);
    }
    unsigned bytes=(at+7)/8;assert(bytes<=8192);
    reset(conf[2]&128?48000:44100);unsigned before=decoded_frames;
    for(unsigned k=0;k<bytes;k+=900) {
        unsigned n=bytes-k;if(n>900)n=900;uint8_t wire[912]={0x80,96,0,0,0,0,0,0,0,0,0,17};
        wire[2]=seq>>8;wire[3]=seq++;if(k+n==bytes)wire[1]|=128;memcpy(wire+12,multi+k,n);btr_aac_media(wire,n+12,conf);
    }
    for(unsigned k=0;k<100 && btr_aac->packet_r!=btr_aac->packet_w;k++){pump();consume();}
    pump();assert(decoded_frames-before==3072 && btr_aac->packet_r==btr_aac->packet_w);
    /* Actual profile SUSPEND flushes pending resample work, not just decode. */
    brc.profile.state=BTR_STREAMING;brc.profile.codec=BTR_CODEC_AAC;brc.profile.seid=8;
    brc.ch[1].remote=71;brc.ch[1].cfg_in=brc.ch[1].cfg_out=1;
    btr_worker_run(btr_resample_job,0);
    uint8_t suspend[]={3,0,65,0,0,9,8};br_acl(2,suspend,sizeof suspend);
    assert(brc.profile.state==BTR_OPEN && !thread_valid && !btr_aac && !btr_out_w);
    br_reset();assert(!thread_valid);
    puts("PASS: multi-AU LATM yields between frames, actual encrypted AVDTP suspend/disconnect joins FIR and releases decoder storage");
}
static void pressure(const char *name)
{
    reset(44100);uint8_t frame[1024];FILE *f=fopen(name,"rb");assert(f);unsigned bytes=fread(frame,1,sizeof frame,f);fclose(f);
    unsigned len=btr_frame_size(frame,bytes,BTR_CAPS);assert(len);uint8_t packet[1100]={0x80,96};packet[12]=1;memcpy(packet+13,frame,len);
    void *all=plat_work_alloc(plat_work_available());assert(all);packet[2]=seq>>8;packet[3]=seq++;
    btr_media(packet,len+13,BTR_CAPS);assert(!btr_sbc);plat_work_free(all);
    __atomic_store_n(&hold_job,1,__ATOMIC_RELEASE);packet[2]=seq>>8;packet[3]=seq++;btr_media(packet,len+13,BTR_CAPS);
    assert(btr_sbc && plat_cpu2_busy());struct btr_sbc_packet head=btr_sbc->packets[btr_sbc->packet_r&15];
    uint8_t immutable[1024];memcpy(immutable,btr_sbc->queue+(head.start&4095),head.bytes);
    unsigned gaps=btr_stats.gaps;
    for(unsigned i=0;i<40;i++) {
        packet[2]=seq>>8;packet[3]=seq++;btr_media(packet,len+13,BTR_CAPS);
        assert(btr_sbc->packet_w-btr_sbc->packet_r<=16 && btr_sbc->queue_w-btr_sbc->queue_r<=4096);
        assert(!memcmp(immutable,btr_sbc->queue+(head.start&4095),head.bytes));
        consume(); /* a genuinely busy/held worker must never make audio wait */
    }
    assert(btr_stats.gaps>gaps);__atomic_store_n(&hold_job,0,__ATOMIC_RELEASE);
    for(unsigned i=0;i<200 && btr_sbc->packet_r!=btr_sbc->packet_w;i++){pump();consume();}
    pump();assert(btr_sbc->packet_r==btr_sbc->packet_w);
    btr_sbc->queue_w=btr_sbc->queue_r=UINT32_MAX-127u;
    btr_sbc->packet_w=btr_sbc->packet_r=UINT32_MAX-1u;
    for(unsigned i=0;i<4;i++) {
        packet[2]=seq>>8;packet[3]=seq++;btr_media(packet,len+13,BTR_CAPS);pump();consume();
    }
    pump();assert(btr_sbc->packet_r==btr_sbc->packet_w && btr_sbc->queue_r==btr_sbc->queue_w);
    reset(44100);assert(plat_work_available()==sizeof work_host_storage);
    puts("PASS: target-path allocation failure, held-worker concurrent IRQ/assembly, immutable overflow drops, chunked queue/cursor wrap and teardown");
}
static void foreground_stall(const char *sbc_name,const char *aac_name)
{
    for(unsigned codec=0;codec<2;codec++) {
        reset(44100);btr_worker_enabled=0;unsigned expected=0,before=decoded_frames;
        if(!codec) {
            FILE *f=fopen(sbc_name,"rb");assert(f);uint8_t data[1024];unsigned bytes=fread(data,1,sizeof data,f);fclose(f);
            unsigned len=btr_frame_size(data,bytes,BTR_CAPS);assert(len);
            unsigned frames=256/len;if(frames>15)frames=15;assert(frames);
            unsigned per=4u*(1u+((data[1]>>4)&3u))*(data[1]&1u?8u:4u);
            btr_audio_reset((data[1]&0xc0)==0xc0?48000:44100);btr_worker_enabled=0;
            for(unsigned n=0;n<16;n++) {
                uint8_t packet[1100]={0x80,96};packet[2]=seq>>8;packet[3]=seq++;packet[12]=(uint8_t)frames;
                for(unsigned k=0;k<frames;k++)memcpy(packet+13+k*len,data,len);
                btr_media(packet,13+frames*len,BTR_CAPS);expected+=frames*per;
            }
        } else {
            FILE *f=fopen(aac_name,"rb");assert(f);uint8_t conf[6],packet[1200];assert(fread(conf,1,6,f)==6);
            btr_audio_reset(conf[2]&128?48000:44100);btr_worker_enabled=0;
            while(!btr_aac || btr_aac->packet_w<8) {
                uint8_t z[4];assert(fread(z,1,4,f)==4);unsigned n=z[0]|z[1]<<8|z[2]<<16|z[3]<<24;
                assert(n && n<=sizeof packet && fread(packet,1,n,f)==n);btr_aac_media(packet,n,conf);
            }
            fclose(f);expected=8192;
        }
        btr_worker_enabled=1;unsigned audio=0,starves=btr_output_underruns;
        /* 64 ms SBC / 120 ms AAC, within each preloaded compressed queue.
         * No foreground decoder polling, allocator or radio parsing.
         * Tick/ALNK service alone must sustain already queued compressed audio. */
        for(unsigned tick=0;tick<(codec?240u:128u);tick++) {
            finish();irq=1;btr_decode_poll_irq();irq=0;finish();
            audio+=44100;if(audio>=512000){audio-=512000;consume();}
        }
        finish();assert(decoded_frames-before>=(expected<4096?expected:3072) && decoded_frames-before<=expected);
        assert(btr_output_underruns==starves && !btr_stats.overruns);
        reset(44100);
    }
    puts("PASS: queued SBC/AAC sustain 64/120-ms foreground stalls using 2-kHz IRQ dispatch, no allocator/joins/DSP in IRQ, no output underruns");
}
static void drift(void)
{
    int16_t input[2048];for(unsigned i=0;i<2048;i++)input[i]=(i&1)?-30000:30000;
    for(unsigned rate=44100;rate<=48000;rate+=3900)for(int ppm=-500;ppm<=500;ppm+=1000) {
        reset(rate);btr_worker_enabled=0;
        core=1;for(unsigned k=0;k<3;k++)btr_pcm_put(input,1024);core=0;
        uint64_t fraction=0;
        for(unsigned block=0;block<4000;block++) {
            int32_t out[512]={0};irq=1;btr_pcm_mix(out,256,4096);irq=0;
            fraction+=(uint64_t)rate*256*(1000000+ppm);
            unsigned frames=fraction/(44100ull*1000000);fraction%=44100ull*1000000;
            assert(frames<=1024 && frames<=btr_pcm_space());
            core=1;btr_pcm_put(input,frames);btr_resample_fill();core=0;
            assert(!btr_output_underruns && btr_out_w-btr_out_r<=BTR_OUT_RING);
        }
        assert(!btr_stats.overruns);
    }
    reset(44100);
    puts("PASS: 16000 asynchronous audio blocks at both rates and +/-500 ppm, combined-ring servo bounded without output underruns or raw overruns");
}
int main(int argc,char **argv)
{
    assert(argc==5);if(!getenv("FM1_SKIP_ORACLE")){oracle();drift();}sbc_stream(argv[1],argv[3]);aac_stream(argv[2],argv[4],0);aac_stream(argv[2],argv[4],1);multi_au(argv[2]);pressure(argv[1]);foreground_stall(argv[1],argv[2]);
    assert(plat_work_available()==sizeof work_host_storage && !locks && !plat_cpu2_busy());
    printf("PASS: %u real threaded bounded jobs, worker/IRQ separation, all scratch released\n",jobs);
}
