/* SPDX-License-Identifier: MPL-2.0 */
#include "bt_receiver.h"
#include "sbc_dec/oi_codec_sbc.h"
static OI_CODEC_SBC_DECODER_CONTEXT btr_decoder;
static uint32_t btr_decoder_data[CODEC_DATA_WORDS(2,SBC_CODEC_FAST_FILTER_BUFFERS)];
static int16_t btr_decoded[256];
static uint8_t btr_fragment[1024],btr_have_sequence,btr_fragmenting;
static uint32_t btr_fragment_n;
static uint8_t btr_fragments_left;
static uint16_t btr_sequence;
#if defined(APP_BT_AAC) && !defined(PLAT_HOST)
#include "bt_receiver_aac.c"
#endif
static int btr_decode(const uint8_t *p,uint32_t n)
{
    uint32_t left=n,bytes=sizeof btr_decoded;
    if(OI_CODEC_SBC_DecodeFrame(&btr_decoder,&p,&left,btr_decoded,&bytes)!=OI_OK || left || bytes>sizeof btr_decoded || bytes%4) {
        btr_stats.decoder_errors++;return 0;
    }
    btr_pcm_put(btr_decoded,bytes/4);btr_stats.frames++;return 1;
}
#ifdef APP_BT_SBC_CPU2
/* Only CPU0 owns the queue cursors/allocator. An executing packet is retained
 * until the mailbox completes; CPU1 only reads its immutable payload. */
#define BTR_SBC_QUEUE_BYTES 4096u
#define BTR_SBC_QUEUE_SLOTS 16u
struct btr_sbc_packet {uint32_t start;uint16_t bytes;uint8_t frames,reset;};
struct btr_sbc_mem {
    uint8_t queue[BTR_SBC_QUEUE_BYTES];
    struct btr_sbc_packet packets[BTR_SBC_QUEUE_SLOTS];
    volatile uint32_t queue_w,queue_r,packet_w,packet_r;uint32_t started;
    uint8_t active,reset_next;
#ifdef APP_BT_RESAMPLE_CPU2
    uint16_t frame_at;uint8_t frame_done;volatile uint8_t job_done;
#endif
};
static struct btr_sbc_mem *btr_sbc;
static void btr_sbc_job(uint32_t arg)
{
    (void)arg;
    struct btr_sbc_mem *m=btr_sbc;
    const struct btr_sbc_packet *q=&m->packets[m->packet_r&(BTR_SBC_QUEUE_SLOTS-1)];
    const uint8_t *p=m->queue+(q->start&(BTR_SBC_QUEUE_BYTES-1));
    uint32_t left=q->bytes;
#ifdef APP_BT_RESAMPLE_CPU2
    p+=m->frame_at;left-=m->frame_at;
    if(!m->frame_done && q->reset)OI_CODEC_SBC_DecoderReset(&btr_decoder,btr_decoder_data,sizeof btr_decoder_data,2,2,FALSE);
    unsigned made=0;
    do {
        uint32_t len=btr_frame_size(p,left,BTR_CAPS);
        if(!len || !btr_decode(p,len)){m->frame_done=q->frames;break;}
        made+=4u*(1u+((p[1]>>4)&3u))*(p[1]&1u?8u:4u);
        p+=len;left-=len;m->frame_at+=(uint16_t)len;m->frame_done++;
    }while(m->frame_done<q->frames && made<128u && btr_pcm_space()>=128u);
    plat_cpu2_sync();m->job_done=1;
#else
    if(q->reset)OI_CODEC_SBC_DecoderReset(&btr_decoder,btr_decoder_data,sizeof btr_decoder_data,2,2,FALSE);
    for(unsigned i=0;i<q->frames;i++) {
        uint32_t len=btr_frame_size(p,left,BTR_CAPS);
        if(!len || !btr_decode(p,len))break;
        p+=len;left-=len;
    }
#endif
}
static void btr_sbc_poll_inner(void)
{
    struct btr_sbc_mem *m=btr_sbc;
    if(!m)return;
#ifdef APP_BT_RESAMPLE_CPU2
    btr_resample_poll();
#endif
    if(m->active) {
#ifdef APP_BT_RESAMPLE_CPU2
        if(!m->job_done)return;
        plat_cpu2_sync();m->active=0;
        const struct btr_sbc_packet *q=&m->packets[m->packet_r&(BTR_SBC_QUEUE_SLOTS-1)];
        if(m->frame_done==q->frames){m->queue_r=q->start+q->bytes;m->packet_r++;m->frame_at=m->frame_done=0;}
#else
        if(plat_cpu2_busy() && plat_ticks24()-m->started<2000000u*PLAT_TICKS_PER_US)return;
        plat_cpu2_wait();plat_cpu2_sync();m->active=0;
        const struct btr_sbc_packet *q=&m->packets[m->packet_r&(BTR_SBC_QUEUE_SLOTS-1)];
        m->queue_r=q->start+q->bytes;m->packet_r++;
#endif
    }
    if(m->packet_r==m->packet_w)return;
#ifdef APP_BT_RESAMPLE_CPU2
    if(btr_pcm_space()<128u)return;
    m->job_done=0;m->active=1;plat_cpu2_sync();
    if(!btr_worker_run(btr_sbc_job,0))m->active=0;
#else
    m->started=plat_ticks24();m->active=1;plat_cpu2_sync();
    if(!plat_cpu2_run(btr_sbc_job,0))m->active=0; /* retry; never decode on CPU0 */
#endif
}
static void btr_sbc_poll(void)
{
#ifdef APP_BT_RESAMPLE_CPU2
    uint32_t lock=plat_lock();btr_sbc_poll_inner();plat_unlock(lock);
#else
    btr_sbc_poll_inner();
#endif
}
static void btr_sbc_submit(const uint8_t *p,uint32_t n,uint32_t frames)
{
    if(!btr_sbc) {
#ifdef APP_BT_RESAMPLE_CPU2
        struct btr_sbc_mem *mem=plat_work_alloc_top(sizeof *mem);
        if(!mem){btr_stats.gaps++;return;}
        memset(mem,0,sizeof *mem);mem->reset_next=1;plat_cpu2_sync();btr_sbc=mem;
#else
        btr_sbc=plat_work_alloc(sizeof *btr_sbc);
        if(!btr_sbc){btr_stats.gaps++;return;}
        memset(btr_sbc,0,sizeof *btr_sbc);btr_sbc->reset_next=1;
#endif
    }
    btr_sbc_poll();
    struct btr_sbc_mem *m=btr_sbc;
    uint32_t w=m->queue_w,at=w&(BTR_SBC_QUEUE_BYTES-1);
    uint32_t skip=n>BTR_SBC_QUEUE_BYTES-at?BTR_SBC_QUEUE_BYTES-at:0;
    if(n>BTR_SBC_QUEUE_BYTES || m->packet_w-m->packet_r>=BTR_SBC_QUEUE_SLOTS ||
       n+skip>BTR_SBC_QUEUE_BYTES-(w-m->queue_r)) {
        btr_stats.gaps++;m->reset_next=1;return; /* drop a whole validated packet */
    }
    struct btr_sbc_packet *q=&m->packets[m->packet_w&(BTR_SBC_QUEUE_SLOTS-1)];
    q->start=w+skip;q->bytes=(uint16_t)n;q->frames=(uint8_t)frames;q->reset=m->reset_next;
    memcpy(m->queue+(q->start&(BTR_SBC_QUEUE_BYTES-1)),p,n);
#ifdef APP_BT_RESAMPLE_CPU2
    uint32_t lock=plat_lock();plat_cpu2_sync();
#endif
    m->reset_next=0;m->queue_w=q->start+n;m->packet_w++;
#ifdef APP_BT_RESAMPLE_CPU2
    plat_unlock(lock);
#endif
    btr_sbc_poll();
}
#else
static void btr_sbc_submit(const uint8_t *p,uint32_t n,uint32_t frames)
{
    for(unsigned i=0;i<frames;i++) {
        uint32_t len=btr_frame_size(p,n,BTR_CAPS);
        if(!len || !btr_decode(p,len))break;
        p+=len;n-=len;
    }
}
#endif
#if defined(APP_BT_APTX) && !defined(PLAT_HOST)
#include "bt_receiver_aptx.c"
#endif
#if defined(APP_BT_LDAC) && !defined(PLAT_HOST)
#include "bt_receiver_ldac.c"
#endif
void btr_media_reset(void)
{
#ifdef APP_BT_RESAMPLE_CPU2
    btr_worker_stop();
#endif
#ifdef APP_BT_SBC_CPU2
    if(btr_sbc) {
        if(btr_sbc->active){plat_cpu2_wait();plat_cpu2_sync();}
        plat_work_free(btr_sbc);btr_sbc=0;
    }
#endif
#if defined(APP_BT_AAC) && !defined(PLAT_HOST)
    btr_aac_reset();
#endif
#if defined(APP_BT_APTX) && !defined(PLAT_HOST)
    btr_aptx_reset();
#endif
#if defined(APP_BT_LDAC) && !defined(PLAT_HOST)
    btr_ldac_reset();
#endif
    btr_have_sequence=btr_fragmenting=0;btr_fragment_n=0;
    OI_CODEC_SBC_DecoderReset(&btr_decoder,btr_decoder_data,sizeof btr_decoder_data,2,2,FALSE);
}
void btr_media(const uint8_t *p,uint32_t n,const uint8_t conf[4])
{
    uint32_t at,ext,frames,len,i;uint8_t flags;uint16_t seq;
    btr_stats.packets++;
    if(n<13 || p[0]>>6!=2)goto bad;
    at=12+4*(p[0]&15);if(at>=n)goto bad;
    if(p[0]&0x10){if(n-at<4)goto bad;ext=((uint32_t)p[at+2]<<8|p[at+3])*4;if(ext>n-at-4)goto bad;at+=4+ext;}
    if(p[0]&0x20){uint32_t pad=p[n-1];if(!pad || pad>n-at)goto bad;n-=pad;}
    if(at>=n)goto bad;
    seq=(uint16_t)((uint16_t)p[2]<<8|p[3]);
    if(btr_have_sequence) {
        int16_t delta=(int16_t)(seq-btr_sequence);
        if(delta<=0)return;
        if(delta!=1){btr_stats.gaps++;btr_fragmenting=0;}
    }
    btr_have_sequence=1;btr_sequence=seq;
    flags=p[at++];if(flags&0x10)goto bad;
    if(flags&0x80) {
        if(!(flags&15))goto bad;
        if(flags&0x40){if(flags&0x20)goto bad;btr_fragmenting=1;btr_fragment_n=0;btr_fragments_left=flags&15;}
        if(!btr_fragmenting || at==n || n-at>sizeof btr_fragment-btr_fragment_n)goto bad;
        if ((flags&15)!=btr_fragments_left || ((flags&0x20)!=0)!=(btr_fragments_left==1)) goto bad;
        btr_fragments_left--;
        for(i=at;i<n;i++)btr_fragment[btr_fragment_n++]=p[i];
        if(!(flags&0x20))return;
        btr_fragmenting=0;
        len=btr_frame_size(btr_fragment,btr_fragment_n,conf);
        if(len!=btr_fragment_n || !len)goto bad;
        btr_sbc_submit(btr_fragment,len,1);return;
    }
    if(flags&0x60 || !(frames=flags&15))goto bad;
    btr_fragmenting=0;
    /* Validate the whole packet before decoding/publishing any PCM. */
    ext=at;
    for(i=0;i<frames;i++){len=btr_frame_size(p+ext,n-ext,conf);if(!len)goto bad;ext+=len;}
    if(ext!=n)goto bad;
    btr_sbc_submit(p+at,n-at,frames);
    return;
bad: btr_stats.malformed++;btr_fragmenting=0;
}

#ifdef APP_BT_RESAMPLE_CPU2
static void btr_decode_poll_irq(void)
{
    uint32_t lock=plat_lock();
    if(btr_worker_enabled) {
        btr_sbc_poll_inner();
#ifdef APP_BT_APTX
        btr_aptx_poll_inner();
#endif
#ifdef APP_BT_LDAC
        btr_ldac_poll_inner();
#endif
#ifdef APP_BT_AAC
        btr_aac_poll_inner();
#endif
    }
    plat_unlock(lock);
}
#endif
