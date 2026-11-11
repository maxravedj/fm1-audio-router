/* SPDX-License-Identifier: MPL-2.0 */
/* Production LDAC RTP/FIFO/CPU1/FIR. Scheduling and IRQs are modelled. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define APP_BT_RESAMPLE_CPU2
#define APP_BT_APTX /* shared exact 24-bit FIR, not libfreeaptx */
#define APP_BT_LDAC
#define APP_AUDIO_MONITOR
#define APP_DYNAMIC_DISPLAY
#define APP_USB_INPUT_WAVEFORM
#define APP_USB_INPUT_SPECTRUM
#include "../include/plat.h"
#define PLAT_TICKS_PER_US 24u
static unsigned core,irq,allocs,fail_alloc,jobs,locks,ticks;
static void (*worker)(uint32_t);static uint32_t argument;
uint32_t plat_ticks24(void){return ++ticks;}
uint32_t plat_ms(void){return ticks/24000u;}
uint32_t plat_lock(void){assert(!core);return locks++;}
void plat_unlock(uint32_t f){assert(locks==f+1);locks=f;}
void plat_cpu2_sync(void){__asm__ volatile("" ::: "memory");}
int plat_cpu2_busy(void){return worker!=0;}
int plat_cpu2_run(void (*fn)(uint32_t),uint32_t arg){assert(!core);if(worker)return 0;worker=fn;argument=arg;return 1;}
int plat_cpu2_wait(void){assert(!irq && !core);if(worker){core=1;worker(argument);core=0;worker=0;jobs++;}return 0;}
void *plat_work_alloc_top(uint32_t n){assert(!irq && !core);if(fail_alloc)return 0;void *p=malloc(n);assert(p);allocs++;return p;}
void plat_work_free(void *p){assert(!core && !irq && !worker && allocs);free(p);allocs--;}
#include "../device/usb_input_meter.h"
#include "../apps/fm1-audio-gateway/meter.h"
#include "../bt/br/bt_receiver_pcm.c"
#include "../bt/br/bt_receiver_ldac.c"
static void btr_decode_poll_irq(void){uint32_t lock=plat_lock();if(btr_worker_enabled)btr_ldac_poll_inner();plat_unlock(lock);}
static void reset(unsigned rate){btr_worker_stop();btr_ldac_reset();btr_audio_reset(rate);memset((void *)&btr_stats,0,sizeof btr_stats);}
static FILE *output;
static unsigned quiet_meter_blocks;
static void consume_prepared(void)
{
    while(btr_out_w-btr_out_r>=256){
        uint32_t expected[2]={0,0},actual[2];int32_t audio[512]={0};
        for(unsigned i=0;i<256;i++)for(unsigned ch=0;ch<2;ch++){
            int32_t v=btr_output[(btr_out_r+i)&(BTR_OUT_RING-1)][ch];
            uint32_t a=v<0?0u-(uint32_t)v:(uint32_t)v;if(a>8388608)a=8388608;
            if(a>expected[ch])expected[ch]=a;
        }
        btr_primed=1;irq=1;btr_pcm_mix(audio,256,4096);irq=0;
        plat_bt_input_peaks(actual);assert(!memcmp(expected,actual,sizeof actual));
        for(unsigned ch=0;ch<2;ch++)if(actual[ch] && actual[ch]<8389){
            gateway_meter_t meter={0};gateway_meter_step(&meter,actual[ch],48);
            assert(!meter.level && !meter.held && meter.peak>0 && meter.peak<=900);
            quiet_meter_blocks++;
        }
    }
}
static void service(void)
{
    unsigned before=btr_w;
    if(worker){core=1;worker(argument);core=0;worker=0;jobs++;}
    if(output)for(unsigned i=before;i!=btr_w;i++)for(unsigned ch=0;ch<2;ch++){
        unsigned v=(uint16_t)btr_pcm[i&(BTR_RING-1)][ch];
        fputc(btr_pcm_low[i&(BTR_RING-1)][ch],output);fputc(v&255,output);fputc(v>>8,output);
    }
    consume_prepared();irq=1;btr_decode_poll_irq();irq=0;
}
static void drain(void)
{
    unsigned guard=10000;
    while(worker || (btr_ldac && btr_ldac->pr!=btr_ldac->pw)){assert(guard--);service();}
}
static uint8_t wire[4200];static uint16_t sequence=65530;
static uint32_t stamp=UINT32_MAX-4095,ssrc=731;
/* Source timestamps can step independently of complete, ordered codec frames.
 * Include the observed +/-128 steps, a non-frame-aligned step and wrap. */
static void jitter_stamp(unsigned packet)
{
    static const int32_t steps[]={128,-128,0,100,-100,4096,-4096,0};
    stamp+=(uint32_t)steps[packet&7u];
}
static void be32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(24-8*i));}
static void send(const uint8_t *p,unsigned n,unsigned flags,int extended)
{
    memset(wire,0,sizeof wire);wire[0]=extended?0xb1:0x80;wire[1]=96;
    wire[2]=(uint8_t)(sequence>>8);wire[3]=(uint8_t)sequence++;be32(wire+4,stamp);be32(wire+8,ssrc);
    unsigned at=12;
    if(extended){at=16;wire[at+3]=1;at+=8;} /* CSRC plus one extension word */
    wire[at++]=(uint8_t)flags;memcpy(wire+at,p,n);at+=n;
    if(extended){wire[at++]=0;wire[at++]=2;}
    btr_ldac_media(wire,at);
    memset(wire,0xa5,sizeof wire); /* accepted payload must be owned by the FIFO */
}
/* Exercise production AVDTP lifecycle with the real allocator/worker/decoder.
 * No radio is modelled; graph preparation must precede reset/reallocation. */
static uint8_t ba_live;
static unsigned prepared;
static void app_bt_ldac_prepare(void){assert(!irq && !core);prepared++;}
static void app_bt_aac_prepare(void){assert(!"unexpected AAC preparation");}
static void btr_aptx_media(const uint8_t *p,uint32_t n,int hd){(void)p;(void)n;(void)hd;assert(!"unexpected aptX media");}
void btr_media(const uint8_t *p,uint32_t n,const uint8_t conf[4]){(void)p;(void)n;(void)conf;assert(!"unexpected SBC media");}
#define APP_BT_AAC_DISPLAY
#define BTR_FLUSH(rate) reset(rate)
#define BTR_SECURE() 1
#define BTR_SEND(p,n) ((void)(p),(void)(n))
#include "../bt/br/bt_receiver_l2cap.c"
static void signal_ldac(const uint8_t *p,unsigned n)
{
    uint8_t frame[40]={0};assert(n<=sizeof frame-4);
    frame[0]=(uint8_t)n;frame[2]=65;memcpy(frame+4,p,n);br_frame(frame,n+4);
}
static void lifecycle(unsigned rate,const uint8_t *frame,unsigned bytes)
{
    FILE *saved_output=output;output=0;br_reset();prepared=0;btr_codec_mask=16;
    brc.ch[1]=(struct btr_channel){.remote=67,.mtu=1008,.cfg_in=1,.cfg_out=1};
    brc.ch[2]=(struct btr_channel){.remote=68,.mtu=1008,.cfg_in=1,.cfg_out=1};
    uint8_t config[]={0,3,20,4,1,0,7,10,0,255,0x2d,1,0,0,0xaa,0,0x20,1};
    config[16]=rate==48000?0x10:0x20;signal_ldac(config,sizeof config);
    assert(brc.profile.state==BTR_CONFIGURED && btr_ldac && btr_rate==rate);
    assert(allocs==1 && !worker && !btr_ldac->checked && prepared==1);
    uint8_t open[]={0,6,20},start[]={0,7,20},pause[]={0,9,20},close[]={0,8,20};
    signal_ldac(open,sizeof open);assert(brc.profile.state==BTR_OPEN && btr_ldac && !worker);
    for(unsigned i=0;i<3;i++){
        signal_ldac(start,sizeof start);assert(ba_live && btr_ldac && allocs==1 && !worker);
        send(frame,bytes,1,0);stamp+=128;drain();assert(!btr_stats.decoder_errors && btr_ldac->checked==1);
        signal_ldac(pause,sizeof pause);
        assert(!ba_live && brc.profile.state==BTR_OPEN && btr_ldac && allocs==1 && !worker);
        assert(!btr_ldac->checked && btr_rate==rate); /* ready while no packets arrive */
    }
    /* A media-channel close also retains the negotiated rate and reservation. */
    uint8_t disconnect[]={BL_DISC_REQ,1,4,0,66,0,68,0};
    br_sig(disconnect,sizeof disconnect);
    assert(brc.profile.state==BTR_CONFIGURED && btr_ldac && btr_rate==rate && allocs==1);
    brc.ch[2]=(struct btr_channel){.remote=68,.mtu=1008,.cfg_in=1,.cfg_out=1};
    signal_ldac(open,sizeof open);signal_ldac(close,sizeof close);
    assert(brc.profile.state==BTR_IDLE && !btr_ldac && !allocs && !worker);
    br_reset();reset(rate);output=saved_output;
}
int main(int argc,char **argv)
{
    assert(argc==5);unsigned rate=(unsigned)atoi(argv[3]),mode=(unsigned)atoi(argv[4]);
    unsigned fragmented=mode&1u,jitter=mode&2u,packets=0;reset(rate);
    FILE *in=fopen(argv[1],"rb"),*expected=fopen(argv[2],"wb");assert(in && expected);
    char output_name[512];assert(strlen(argv[2])<490);snprintf(output_name,sizeof output_name,"%s.actual",argv[2]);
    output=fopen(output_name,"wb");assert(output);
    uint8_t frame[FM1_LDAC_MAX_FRAME],bundle[4*FM1_LDAC_MAX_FRAME],reference[128*6];
    unsigned held=0,bytes=0,count=0,last=0;
    while(1){
        int lo=fgetc(in);if(lo<0)break;int hi=fgetc(in);assert(hi>=0);unsigned n=lo+(hi<<8);
        assert(n<=sizeof frame && fread(frame,1,n,in)==n && fread(reference,1,sizeof reference,in)==sizeof reference);
        if(!count)lifecycle(rate,frame,n);
        assert(fm1_ldac_frame_size(frame,n,rate)==n);last=n;
        assert(fwrite(reference,1,sizeof reference,expected)==sizeof reference);count++;
        if(fragmented){
            if(jitter)jitter_stamp(packets);packets++;
            unsigned part=n/3;send(frame,part,0xc3,0);send(frame+part,part,0x82,1);send(frame+2*part,n-2*part,0xa1,0);
            stamp+=128;drain();
        }else{
            memcpy(bundle+bytes,frame,n);bytes+=n;held++;
            if(held==(jitter?1u+(packets&3u):3u)){
                if(jitter)jitter_stamp(packets);packets++;
                send(bundle,bytes,held,count&1);stamp+=held*128;drain();bytes=held=0;
            }
        }
    }
    if(held){if(jitter)jitter_stamp(packets);send(bundle,bytes,held,1);stamp+=held*128;drain();}
    fclose(in);fclose(expected);fclose(output);output=0;
    if(strstr(argv[1],"quiet"))assert(quiet_meter_blocks>100);
    assert(count>100 && btr_stats.frames==count && btr_ldac_decode_jobs==count);
    assert(!btr_stats.malformed && !btr_stats.decoder_errors && !btr_stats.gaps && !btr_stats.overruns);
    assert(!btr_ldac_diag.sequence_gaps && !btr_ldac_diag.queue_drops);
    assert(jitter?btr_ldac_diag.timestamp_jumps>10:!btr_ldac_diag.timestamp_jumps);
    assert(btr_ldac_diag.history_resets==1 && btr_ldac_diag.max_bytes<=BTR_LDAC_QUEUE && btr_ldac_diag.max_packets<=BTR_LDAC_SLOTS);
    assert(btr_ldac_diag.check==3 && btr_ldac->checked==1);
    assert(allocs==1 && btr_pcm_low && jobs);
    /* Duplication/reordering are ignored; only confirmed loss/sender changes
     * reset history. A timestamp-only jump stays diagnostic. */
    reset(rate);send(frame,last,1,0);stamp+=128;drain();unsigned frames=btr_stats.frames;
    sequence--;send(frame,last,1,0);drain();assert(btr_stats.frames==frames);
    sequence-=2;send(frame,last,1,0);drain();assert(btr_stats.frames==frames);
    assert(btr_ldac_diag.reordered==2);
    sequence+=5;send(frame,last,1,0);stamp+=128;drain();assert(btr_stats.gaps);
    assert(btr_ldac_diag.sequence_gaps==1 && !btr_ldac_diag.timestamp_jumps && !btr_ldac_diag.queue_drops);
    unsigned gaps=btr_stats.gaps,resets=btr_ldac_diag.history_resets;
    stamp+=100;send(frame,last,1,0);stamp+=128;drain();assert(btr_stats.gaps==gaps);
    assert(btr_ldac_diag.timestamp_jumps==1 && btr_ldac_diag.received_stamp-btr_ldac_diag.expected_stamp==100);
    assert(btr_ldac_diag.history_resets==resets);
    ssrc++;sequence=1;send(frame,last,1,0);stamp+=128;drain();
    assert(btr_ldac_diag.sender_changes==1 && btr_ldac_diag.history_resets==3);
    /* Compressed byte/descriptor cursors wrap independently of RTP sequence. */
    btr_ldac->qw=btr_ldac->qr=UINT32_MAX-100u;
    btr_ldac->pw=btr_ldac->pr=UINT32_MAX-3u;
    for(unsigned i=0;i<40;i++){send(frame,last,1,0);stamp+=128;drain();}
    assert(btr_ldac->qw==btr_ldac->qr && btr_ldac->pw==btr_ldac->pr && !btr_stats.decoder_errors);
    /* Lost/interleaved fragments, wrong countdown/timestamp, oversized input. */
    unsigned malformed=btr_stats.malformed;
    send(frame,20,0xc3,0);sequence++;send(frame+20,20,0xa1,0);assert(btr_stats.malformed>malformed);
    send(frame,20,0xc2,0);stamp++;send(frame+20,last-20,0xa1,0);assert(!btr_ldac->fragmenting);
    send(frame,last,2,0);assert(btr_stats.malformed>=malformed+3);
    send(frame,last,0x51,0);send(frame,last,0x11,0);
    for(unsigned n=0;n<14;n++)btr_ldac_media(wire,n);
    /* Decode errors produce timed silence without publishing invalid PCM. */
    memset(frame+3,0xff,last-3);send(frame,last,1,0);stamp+=128;drain();assert(btr_stats.decoder_errors);
    /* Failure/retry and a saturated FIFO never allocate or wait in IRQ/CPU1. */
    reset(rate);fail_alloc=1;send(frame,last,1,0);assert(!worker && !allocs);fail_alloc=0;
    assert(btr_ldac_diag.alloc_failures==1 && !btr_ldac_diag.sequence_gaps && !btr_ldac_diag.timestamp_jumps);
    for(unsigned i=0;i<100;i++){send(frame,last,1,0);stamp+=128;}
    assert(allocs==1 && worker && btr_stats.gaps && btr_ldac_diag.queue_drops);drain();
    for(unsigned row=0;row<5;row++){
        uint32_t a,b,c;const char *label=btr_ldac_debug(row,&a,&b,&c);assert(label);
        char line[64];int n=snprintf(line,sizeof line,"%s%u %u %u",label,a,b,c);assert(n>0 && n<=43);
        /* Even three maximum-width uint32 counters fit the log payload. */
        assert(strlen(label)+32<=43);
    }
    reset(rate);assert(!allocs && !worker && !btr_pcm_low && !locks);
    for(unsigned row=0;row<5;row++){uint32_t a,b,c;assert(btr_ldac_debug(row,&a,&b,&c) && !a && !b && !c);}
    puts("PASS: LDAC RTP bundling/fragments/extensions/padding, sequence/timestamp rollover/loss, immutable CPU1 FIFO, 24-bit output, errors/silence, saturation, allocation failure and joined teardown");
    puts("PASS: LDAC sequence/FIFO/timestamp/allocation diagnostics distinguish failure causes, track actual history resets, fit log payload and clear on teardown");
    if(jitter)puts("PASS: ordered LDAC frames retain exact upstream PCM across timestamp-only steps, variable packet counts and wrap, including fragmented frames");
    puts("PASS: LDAC CONFIGURED/OPEN/STREAMING/SUSPEND reserves workspace before media; joined pause/resume and channel-close lifecycle preserve rate and free on CLOSE");
    puts("PASS: real CPU1 LDAC decode/FIR -> CPU0 mixer -> device BT peak snapshot retains quiet 24-bit samples below the graphical floor");
}
