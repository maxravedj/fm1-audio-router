/* SPDX-License-Identifier: MPL-2.0 */
/* CPU0 validates/queues complete A2DP payloads; CPU1 alone owns libfreeaptx
 * state and publishes full 24-bit PCM through the shared rate matcher. */
#include "aptx_dec/freeaptx.h"
#define BTR_APTX_QUEUE 4096u
#define BTR_APTX_SLOTS 16u
#define BTR_APTX_JOB_WORDS 64u /* 256 PCM frames; amortize CPU0/CPU1 handoff */
struct btr_aptx_packet {uint32_t start;uint16_t bytes;uint8_t reset,pad;};
struct btr_aptx_mem {
    uint8_t queue[BTR_APTX_QUEUE],low[BTR_RING][2];
    struct btr_aptx_packet packets[BTR_APTX_SLOTS];
    uint32_t qw,qr,pw,pr,ssrc;
    uint16_t at,seq;
    uint8_t hd,active,reset_next,have_seq;
    volatile uint8_t done;
};
static struct btr_aptx_mem *btr_aptx;
static struct aptx_context *btr_aptx_decoder;
static uint32_t btr_aptx_bytes(void){return ((sizeof(struct btr_aptx_mem)+7u)&~7u)+(uint32_t)aptx_context_size();}
static void btr_aptx_put(const uint8_t *p,unsigned frames)
{
    unsigned w=btr_w;
    for(unsigned i=0;i<frames;i++)for(unsigned ch=0;ch<2;ch++){
        const uint8_t *v=p+(2*i+ch)*3;
        btr_pcm[(w+i)&(BTR_RING-1)][ch]=(int16_t)((unsigned)v[1]|(unsigned)v[2]<<8);
        btr_pcm_low[(w+i)&(BTR_RING-1)][ch]=v[0];
    }
    plat_cpu2_sync();btr_w=w+frames;btr_resample_fill();
}
static void btr_aptx_job(uint32_t unused)
{
    (void)unused;
    struct btr_aptx_mem *m=btr_aptx;
    const struct btr_aptx_packet *q=&m->packets[m->pr&(BTR_APTX_SLOTS-1)];
    unsigned frame=m->hd?6:4;
    if(!m->at && q->reset)aptx_reset(btr_aptx_decoder);
    unsigned n=q->bytes-m->at;if(n>BTR_APTX_JOB_WORDS*frame)n=BTR_APTX_JOB_WORDS*frame;
    unsigned pos=(q->start+m->at)&(BTR_APTX_QUEUE-1);
    if(n>BTR_APTX_QUEUE-pos)n=BTR_APTX_QUEUE-pos;
    uint8_t pcm[(BTR_APTX_JOB_WORDS+1)*24];size_t written=0,dropped=0;int synced;
    uint32_t decode_start=plat_ticks24();
    /* The library carries a codeword split at the byte-ring boundary. */
    size_t used=aptx_decode_sync(btr_aptx_decoder,m->queue+pos,n,pcm,sizeof pcm,&written,&synced,&dropped);
    unsigned us=(plat_ticks24()-decode_start)/PLAT_TICKS_PER_US;
    btr_aptx_decode_us+=us;btr_aptx_decode_jobs++;if(us>btr_aptx_decode_max)btr_aptx_decode_max=us;
    if(dropped)btr_stats.decoder_errors++;
    if(used!=n || written%6 || written>sizeof pcm){btr_stats.decoder_errors++;m->at=q->bytes;}
    else {btr_aptx_put(pcm,(unsigned)written/6);btr_stats.frames+=(unsigned)written/24;m->at+=(uint16_t)used;}
    plat_cpu2_sync();m->done=1;
}
static void btr_aptx_poll_inner(void)
{
    struct btr_aptx_mem *m=btr_aptx;if(!m)return;
    btr_resample_poll();
    if(m->active){if(!m->done)return;plat_cpu2_sync();m->active=0;
        const struct btr_aptx_packet *q=&m->packets[m->pr&(BTR_APTX_SLOTS-1)];
        if(m->at==q->bytes){m->qr=q->start+q->bytes;m->pr++;m->at=0;}}
    if(m->pr==m->pw || btr_pcm_space()<4*(BTR_APTX_JOB_WORDS+1))return;
    m->done=0;m->active=1;plat_cpu2_sync();
    if(!btr_worker_run(btr_aptx_job,0))m->active=0;
}
static void btr_aptx_poll(void){uint32_t lock=plat_lock();btr_aptx_poll_inner();plat_unlock(lock);}
static void btr_aptx_reset(void)
{
    /* btr_media_reset stopped/joined the shared worker before reaching here. */
    uint32_t lock=plat_lock();
    /* The audio consumer must never interpret queued 24-bit output as 16-bit
     * between freeing codec state and the subsequent stream reset. */
    btr_out_r=btr_out_w=0;btr_primed=0;btr_pcm_low=0;
    plat_unlock(lock);btr_aptx_decoder=0;
    btr_aptx_decode_us=btr_aptx_decode_max=btr_aptx_decode_jobs=0;
    btr_aptx_fir_us=btr_aptx_fir_max=btr_aptx_fir_blocks=0;
    if(btr_aptx){plat_work_free(btr_aptx);btr_aptx=0;}
}
static void btr_aptx_media(const uint8_t *p,uint32_t n,unsigned hd)
{
    unsigned at=0,reset=0;uint16_t seq=0;uint32_t ssrc=0;
    btr_stats.packets++;
    /* Standard aptX is raw L2CAP, WITHOUT RTP. aptX HD carries RTP but no
     * SBC-style media header. Never guess format from compressed data. */
    if(hd){
        if(n<12 || p[0]>>6!=2)goto bad;
        at=12+4*(p[0]&15);if(at>n)goto bad;
        if(p[0]&0x10){if(n-at<4)goto bad;unsigned ext=((unsigned)p[at+2]<<8|p[at+3])*4;if(ext>n-at-4)goto bad;at+=4+ext;}
        if(p[0]&0x20){unsigned pad=p[n-1];if(!pad || pad>n-at)goto bad;n-=pad;}
        seq=(uint16_t)((unsigned)p[2]<<8|p[3]);
        for(unsigned i=8;i<12;i++)ssrc=ssrc<<8|p[i];
    }
    if(at>=n || (n-at)%(hd?6:4) || n-at>1008)goto bad;
    if(!btr_aptx){
        struct btr_aptx_mem *m=plat_work_alloc_top(btr_aptx_bytes());if(!m){btr_stats.gaps++;return;}
        memset(m,0,sizeof *m);m->hd=(uint8_t)hd;m->reset_next=1;
        btr_aptx_decoder=aptx_init_storage((uint8_t *)m+((sizeof *m+7u)&~7u),hd);
        btr_pcm_low=m->low;plat_cpu2_sync();btr_aptx=m;
    }
    struct btr_aptx_mem *m=btr_aptx;
    if(m->hd!=hd)goto bad;
    if(hd && m->have_seq && m->ssrc==ssrc){int16_t delta=(int16_t)(seq-m->seq);if(delta<=0)return;if(delta!=1){reset=1;btr_stats.gaps++;}}
    else if(hd && m->have_seq)reset=1;
    if(hd){m->seq=seq;m->ssrc=ssrc;m->have_seq=1;}
    btr_aptx_poll();n-=at;
    unsigned pos=m->qw&(BTR_APTX_QUEUE-1);
    if(m->pw-m->pr==BTR_APTX_SLOTS || n>BTR_APTX_QUEUE-(m->qw-m->qr)){m->reset_next=1;btr_stats.gaps++;return;}
    struct btr_aptx_packet *q=&m->packets[m->pw&(BTR_APTX_SLOTS-1)];
    q->start=m->qw;q->bytes=(uint16_t)n;q->reset=(uint8_t)(reset||m->reset_next);
    unsigned first=BTR_APTX_QUEUE-pos;if(first>n)first=n;
    memcpy(m->queue+pos,p+at,first);
    if(first<n)memcpy(m->queue,p+at+first,n-first);
    uint32_t lock=plat_lock();m->reset_next=0;m->qw=q->start+n;plat_cpu2_sync();m->pw++;plat_unlock(lock);
    btr_aptx_poll();return;
bad:btr_stats.malformed++;
}
