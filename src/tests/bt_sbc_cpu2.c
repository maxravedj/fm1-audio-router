/* SPDX-License-Identifier: MPL-2.0 */
/* Actual SBC parser/decoder/ring with a held or genuinely threaded worker. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#ifndef TEST_SYNC
#define APP_BT_SBC_CPU2
#endif
#define PLAT_TICKS_PER_US 24u
static uint32_t plat_ticks24(void){static uint32_t t;return ++t;}
static __thread unsigned test_core;
static void (*worker_fn)(uint32_t);
static uint32_t worker_arg;
static unsigned jobs,allocations,fail_alloc,thread_running;
static pthread_t thread;
static void *thread_job(void *unused)
{(void)unused;test_core=1;worker_fn(worker_arg);return 0;}
static int plat_cpu2_run(void (*fn)(uint32_t),uint32_t arg)
{assert(!test_core && !worker_fn);worker_fn=fn;worker_arg=arg;return 1;}
static int plat_cpu2_busy(void){return worker_fn!=0;}
static void plat_cpu2_sync(void){__asm__ volatile("" ::: "memory");}
static int plat_cpu2_wait(void)
{
    assert(!test_core);
    if(worker_fn) {
        if(thread_running){assert(!pthread_join(thread,0));thread_running=0;}
        else {test_core=1;worker_fn(worker_arg);test_core=0;}
        worker_fn=0;jobs++;
    }
    return 0;
}
static void *plat_work_alloc(uint32_t n){assert(!test_core);if(fail_alloc)return 0;void *p=calloc(1,n);assert(p);allocations++;return p;}
static void plat_work_free(void *p){assert(!test_core);if(p){assert(allocations);allocations--;free(p);}}
#include "../bt/br/bt_receiver_pcm.c"
#include "../bt/br/sbc_dec/oi_codec_sbc.h"
static FILE *output;
static uint32_t output_frames;
static OI_STATUS checked_decode(OI_CODEC_SBC_DECODER_CONTEXT *c,const OI_BYTE **p,uint32_t *n,int16_t *pcm,uint32_t *bytes)
{
#ifdef APP_BT_SBC_CPU2
    assert(test_core==1);
#else
    assert(!test_core);
#endif
    return OI_CODEC_SBC_DecodeFrame(c,p,n,pcm,bytes);
}
static void checked_put(const int16_t *p,uint32_t n)
{if(output)assert(fwrite(p,4,n,output)==n);output_frames+=n;btr_pcm_put(p,n);}
#define OI_CODEC_SBC_DecodeFrame checked_decode
#define btr_pcm_put checked_put
#include "../bt/br/bt_receiver_media.c"
#undef btr_pcm_put
#undef OI_CODEC_SBC_DecodeFrame
#include "../bt/br/bt_receiver_profile.c"
static uint32_t seq;
static void packet(const uint8_t *p,uint32_t n,uint8_t flags)
{
    uint8_t wire[1200]={0x80,96};assert(n+13<=sizeof wire);
    wire[2]=(uint8_t)(seq>>8);wire[3]=(uint8_t)seq++;wire[12]=flags;memcpy(wire+13,p,n);
    btr_media(wire,n+13,BTR_CAPS);
    memset(wire,0xa5,sizeof wire); /* caller's reassembly storage is immediately reused */
}
static void drain(void)
{
#ifdef APP_BT_SBC_CPU2
    btr_sbc_poll();
    while(worker_fn){plat_cpu2_wait();btr_r=btr_w;btr_sbc_poll();}
#endif
    btr_r=btr_w;
}
int main(int argc,char **argv)
{
    assert(argc==3);FILE *f=fopen(argv[1],"rb");assert(f);
    static uint8_t stream[200000];uint32_t size=fread(stream,1,sizeof stream,f);fclose(f);assert(size);
    output=fopen(argv[2],"wb");assert(output);btr_media_reset();btr_audio_reset(44100);seq=65530;
    /* Up to 15 frames per packet, including all supported SBC block/subband modes. */
    for(uint32_t at=0,batch=0;at<size;batch++) {
        uint32_t bytes=0,frames=0;
        while(at+bytes<size && frames<(batch%15)+1) {
            uint32_t n=btr_frame_size(stream+at+bytes,size-at-bytes,BTR_CAPS);assert(n);
            if(bytes+n>990)break;bytes+=n;frames++;
        }
        assert(frames);packet(stream+at,bytes,(uint8_t)frames);at+=bytes;
#ifdef APP_BT_SBC_CPU2
        /* Real concurrent assembly against a retained executing payload. */
        if(batch%3==0 && worker_fn){assert(!pthread_create(&thread,0,thread_job,0));thread_running=1;}
        if(batch%3==2 || at==size)drain();
#else
        drain();
#endif
    }
    drain();assert(!btr_stats.malformed && !btr_stats.decoder_errors && !btr_stats.gaps && !btr_stats.overruns);
    fclose(output);output=0;assert(output_frames);
#ifdef APP_BT_SBC_CPU2
    assert(jobs && allocations==1);
    btr_media_reset();assert(!allocations && !worker_fn);
    uint32_t n=btr_frame_size(stream,size,BTR_CAPS),half=n/2;
    /* Fragment lifetime, duplicate suppression and a malformed full packet. */
    seq=65535;uint32_t before=output_frames;
    packet(stream,half,0xc2);assert(!worker_fn);
    packet(stream+half,n-half,0xa1);assert(worker_fn && output_frames==before);drain();
    assert(output_frames>before);before=output_frames;
    seq--;packet(stream+half,n-half,0xa1);assert(!worker_fn && output_frames==before);
    uint8_t bad[1100];memcpy(bad,stream,n);bad[n]=0;
    packet(bad,n+1,2);assert(!worker_fn && btr_stats.malformed);
    /* Pending input never overwrites an executing packet under byte/slot pressure. */
    packet(stream,n,1);assert(worker_fn);
    uint8_t saved[1024];struct btr_sbc_packet q=btr_sbc->packets[btr_sbc->packet_r&15];
    memcpy(saved,btr_sbc->queue+(q.start&4095),q.bytes);
    assert(!pthread_create(&thread,0,thread_job,0));thread_running=1;
    for(unsigned i=0;i<100;i++) {
        packet(stream,n,1);
        assert(btr_sbc->packet_w-btr_sbc->packet_r<=16 && btr_sbc->queue_w-btr_sbc->queue_r<=4096);
        assert(!memcmp(saved,btr_sbc->queue+(q.start&4095),q.bytes));
    }
    assert(btr_stats.gaps);btr_media_reset();btr_audio_reset(44100);
    assert(!worker_fn && !allocations);before=output_frames;drain();assert(output_frames==before);
    /* Both cursor families wrap, including contiguous padding at the byte-ring end. */
    packet(stream,n,1);drain();
    btr_sbc->queue_w=btr_sbc->queue_r=UINT32_MAX-127;
    btr_sbc->packet_w=btr_sbc->packet_r=UINT32_MAX-1;
    for(unsigned i=0;i<6;i++){packet(stream,n,1);drain();}
    assert(btr_sbc->queue_w==btr_sbc->queue_r && btr_sbc->packet_w==btr_sbc->packet_r);
    btr_media_reset();btr_audio_reset(44100);fail_alloc=1;before=output_frames;
    packet(stream,n,1);assert(!btr_sbc && !worker_fn && output_frames==before);
    fail_alloc=0;packet(stream,n,1);drain();assert(output_frames>before);
    /* CRC errors stay on CPU1 and the next valid frame can recover. */
    memcpy(bad,stream,n);bad[3]^=1;before=btr_stats.decoder_errors;
    packet(bad,n,1);drain();assert(btr_stats.decoder_errors==before+1);
    packet(stream,n,1);drain();btr_media_reset();assert(!allocations && !worker_fn);
    printf("PASS: SBC CPU2 actual decoder/thread, immutable packet bursts, bounds, wrap, allocation failure, CRC, fragments and join-before-free; %u bytes scratch\n",(unsigned)sizeof(struct btr_sbc_mem));
#else
    puts("PASS: synchronous SBC PCM reference");
#endif
    return 0;
}
