/* SPDX-License-Identifier: MPL-2.0 */
/* CPU0 validates RTP/media and owns the bounded compressed FIFO. CPU1 alone
 * decodes complete immutable frames and runs the shared exact 24-bit FIR. */
#include "ldac_dec/ldac_dec.h"
#define BTR_LDAC_QUEUE 4096u
#define BTR_LDAC_SLOTS 16u
struct btr_ldac_packet {uint32_t start;uint16_t bytes;uint8_t frames,reset;};
struct btr_ldac_mem {
    uint8_t queue[BTR_LDAC_QUEUE],low[BTR_RING][2],fragment[FM1_LDAC_MAX_FRAME];
    int32_t pcm[FM1_LDAC_SAMPLES][2];
    struct btr_ldac_packet packets[BTR_LDAC_SLOTS];
    fm1_ldac_dec *decoder;
    uint32_t qw,qr,pw,pr,ssrc,next_stamp,fragment_stamp;
    uint16_t seq,at,fragment_bytes;
    uint8_t frames_done,fragments_left,fragmenting,have_seq,have_stamp;
    uint8_t active,reset_next,checked;
    volatile uint8_t done;
};
static struct btr_ldac_mem *btr_ldac;
static uint32_t btr_ldac_decode_us,btr_ldac_decode_max,btr_ldac_decode_jobs;
/* Metadata only. CPU0 owns transport counters, CPU1 owns history resets.
 * Timestamp mismatches alone do not establish missing compressed frames. */
static volatile struct {
    uint32_t sequence_gaps,queue_drops,timestamp_jumps;
    uint32_t expected_stamp,received_stamp,jump_frames;
    uint32_t max_bytes,max_packets,alloc_failures;
    uint32_t history_resets,reordered,sender_changes;
    uint32_t check,hash[2];
} btr_ldac_diag;
static const char *btr_ldac_debug(unsigned row,uint32_t *a,uint32_t *b,uint32_t *c)
{
    switch(row){
    case 0:*a=btr_ldac_diag.sequence_gaps;*b=btr_ldac_diag.queue_drops;*c=btr_ldac_diag.timestamp_jumps;return "LDAC loss ";
    case 1:*a=btr_ldac_diag.expected_stamp;*b=btr_ldac_diag.received_stamp;*c=btr_ldac_diag.jump_frames;return "LDAC jump ";
    case 2:*a=btr_ldac_diag.max_bytes;*b=btr_ldac_diag.max_packets;*c=btr_ldac_diag.alloc_failures;return "LDAC buf ";
    case 3:*a=btr_ldac_diag.history_resets;*b=btr_ldac_diag.reordered;*c=btr_ldac_diag.sender_changes;return "LDAC hist ";
    case 4:*a=btr_ldac_diag.check;*b=btr_ldac_diag.hash[0];*c=btr_ldac_diag.hash[1];return "LDAC check ";
    default:return 0;
    }
}
static void btr_ldac_timestamp(struct btr_ldac_mem *m,uint32_t stamp,unsigned frames)
{
    if(m->have_stamp && stamp!=m->next_stamp){
        /* Some Android sources step RTP time despite consecutive sequence
         * numbers and intact codec frames. Preserve transform overlap: frame
         * counts drive PCM time; actual loss/error paths request their own
         * reset. Timestamps remain strict within a fragmented codec frame. */
        btr_ldac_diag.timestamp_jumps++;
        btr_ldac_diag.expected_stamp=m->next_stamp;
        btr_ldac_diag.received_stamp=stamp;btr_ldac_diag.jump_frames=frames;
    }
}
static uint32_t btr_ldac_bytes(void)
{
    return ((sizeof(struct btr_ldac_mem)+7u)&~7u)+(uint32_t)fm1_ldac_dec_size();
}
static void btr_ldac_put(struct btr_ldac_mem *m)
{
    uint32_t w=btr_w;
    for(unsigned i=0;i<FM1_LDAC_SAMPLES;i++)for(unsigned ch=0;ch<2;ch++){
        uint32_t v=(uint32_t)m->pcm[i][ch],at=(w+i)&(BTR_RING-1u);
        btr_pcm[at][ch]=(int16_t)(v>>8);btr_pcm_low[at][ch]=(uint8_t)v;
    }
    plat_cpu2_sync();btr_w=w+FM1_LDAC_SAMPLES;btr_resample_fill();
}
static void btr_ldac_job(uint32_t unused)
{
    (void)unused;struct btr_ldac_mem *m=btr_ldac;
    if(!m->checked){
        uint32_t hash[2]={0,0};
        unsigned check=fm1_ldac_dec_check(m->decoder,m->pcm,hash);
        btr_ldac_diag.hash[0]=hash[0];btr_ldac_diag.hash[1]=hash[1];btr_ldac_diag.check=check;
        m->checked=check==3?1:2;
        plat_cpu2_sync();m->done=1;return; /* two frames; no live PCM published */
    }
    const struct btr_ldac_packet *q=&m->packets[m->pr&(BTR_LDAC_SLOTS-1u)];
    if(!m->frames_done && q->reset){fm1_ldac_dec_reset(m->decoder);btr_ldac_diag.history_resets++;}
    unsigned made=0;
    do {
        const uint8_t *p=m->queue+(q->start&(BTR_LDAC_QUEUE-1u))+m->at;
        unsigned bytes=(unsigned)fm1_ldac_frame_size(p,q->bytes-m->at,btr_rate);
        uint32_t start=plat_ticks24();
        int result=m->checked==2?FM1_LDAC_DATA:bytes?fm1_ldac_dec_frame(m->decoder,p,bytes,m->pcm,FM1_LDAC_SAMPLES):FM1_LDAC_FRAME;
        unsigned us=(plat_ticks24()-start)/PLAT_TICKS_PER_US;
        btr_ldac_decode_us+=us;btr_ldac_decode_jobs++;
        if(us>btr_ldac_decode_max)btr_ldac_decode_max=us;
        if(result!=FM1_LDAC_SAMPLES){
            btr_stats.decoder_errors++;fm1_ldac_dec_reset(m->decoder);btr_ldac_diag.history_resets++;
            /* Preserve sample time on a bad frame; never publish suspect PCM. */
            memset(m->pcm,0,sizeof m->pcm);
        }
        btr_ldac_put(m);btr_stats.frames++;made+=FM1_LDAC_SAMPLES;
        m->at+=(uint16_t)bytes;m->frames_done++;
        if(!bytes){m->frames_done=q->frames;break;}
    }while(m->frames_done<q->frames && made<256u && btr_pcm_space()>=FM1_LDAC_SAMPLES);
    plat_cpu2_sync();m->done=1;
}
static void btr_ldac_poll_inner(void)
{
    struct btr_ldac_mem *m=btr_ldac;if(!m)return;
    btr_resample_poll();
    if(m->active){
        if(!m->done)return;plat_cpu2_sync();m->active=0;
        const struct btr_ldac_packet *q=&m->packets[m->pr&(BTR_LDAC_SLOTS-1u)];
        if(m->frames_done==q->frames){m->qr=q->start+q->bytes;m->pr++;m->at=m->frames_done=0;}
    }
    if(m->pr==m->pw || btr_pcm_space()<FM1_LDAC_SAMPLES)return;
    m->active=1;m->done=0;plat_cpu2_sync();
    if(!btr_worker_run(btr_ldac_job,0))m->active=0;
}
static void btr_ldac_poll(void)
{
    uint32_t lock=plat_lock();btr_ldac_poll_inner();plat_unlock(lock);
}
static void btr_ldac_reset(void)
{
    /* Caller already stopped dispatch and joined CPU1. */
    struct btr_ldac_mem *m=btr_ldac;
    if(m){
        uint32_t lock=plat_lock();btr_out_r=btr_out_w=0;btr_primed=0;
        btr_pcm_low=0;btr_ldac=0;plat_unlock(lock);
        fm1_ldac_dec_destroy(m->decoder);plat_work_free(m);
    }
    memset((void *)&btr_ldac_diag,0,sizeof btr_ldac_diag);
    btr_ldac_decode_us=btr_ldac_decode_max=btr_ldac_decode_jobs=0;
}
static int btr_ldac_allocate(void)
{
    if(btr_ldac)return 1;
    struct btr_ldac_mem *m=plat_work_alloc_top(btr_ldac_bytes());
    if(!m){btr_stats.gaps++;btr_ldac_diag.alloc_failures++;return 0;}
    memset(m,0,sizeof *m);
    m->decoder=fm1_ldac_dec_init((uint8_t *)m+((sizeof *m+7u)&~7u),fm1_ldac_dec_size(),btr_rate);
    if(!m->decoder){plat_work_free(m);btr_stats.decoder_errors++;return 0;}
    m->reset_next=1;
    uint32_t lock=plat_lock();btr_pcm_low=m->low;plat_cpu2_sync();btr_ldac=m;plat_unlock(lock);
    return 1;
}
static int btr_ldac_submit(struct btr_ldac_mem *m,const uint8_t *p,unsigned bytes,unsigned frames)
{
    btr_ldac_poll();unsigned pos=m->qw&(BTR_LDAC_QUEUE-1u);
    unsigned skip=bytes>BTR_LDAC_QUEUE-pos?BTR_LDAC_QUEUE-pos:0;
    if(bytes>BTR_LDAC_QUEUE || m->pw-m->pr>=BTR_LDAC_SLOTS ||
       bytes+skip>BTR_LDAC_QUEUE-(m->qw-m->qr)){
        m->reset_next=1;btr_stats.gaps++;btr_ldac_diag.queue_drops++;return 0;
    }
    struct btr_ldac_packet *q=&m->packets[m->pw&(BTR_LDAC_SLOTS-1u)];
    q->start=m->qw+skip;q->bytes=(uint16_t)bytes;q->frames=(uint8_t)frames;q->reset=m->reset_next;
    memcpy(m->queue+(q->start&(BTR_LDAC_QUEUE-1u)),p,bytes);
    uint32_t lock=plat_lock();m->reset_next=0;m->qw=q->start+bytes;
    plat_cpu2_sync();m->pw++;
    if(m->qw-m->qr>btr_ldac_diag.max_bytes)btr_ldac_diag.max_bytes=m->qw-m->qr;
    if(m->pw-m->pr>btr_ldac_diag.max_packets)btr_ldac_diag.max_packets=m->pw-m->pr;
    plat_unlock(lock);btr_ldac_poll();return 1;
}
static void btr_ldac_media(const uint8_t *p,uint32_t n)
{
    unsigned at,flags,frames;uint16_t seq;uint32_t ssrc=0,stamp=0;
    btr_stats.packets++;
    if(n<14 || p[0]>>6!=2)goto bad;
    at=12+4u*(p[0]&15u);if(at>=n)goto bad;
    if(p[0]&0x10){
        if(n-at<4)goto bad;
        unsigned ext=((unsigned)p[at+2]<<8|p[at+3])*4u;
        if(ext>n-at-4)goto bad;at+=4+ext;
    }
    if(p[0]&0x20){unsigned pad=p[n-1];if(!pad || pad>n-at)goto bad;n-=pad;}
    if(at>=n)goto bad;
    flags=p[at++];frames=flags&15u;
    if((flags&0x10) || !frames || at==n || n-at>BTR_LDAC_QUEUE)goto bad;
    if(!(flags&0x80)){
        if(flags&0x60)goto bad;
        unsigned used=at;
        for(unsigned f=0;f<frames;f++){
            unsigned len=(unsigned)fm1_ldac_frame_size(p+used,n-used,btr_rate);
            if(!len)goto bad;used+=len;
        }
        if(used!=n)goto bad;
    }else if((flags&0x40) && ((flags&0x20) || frames<2))goto bad;
    seq=(uint16_t)((unsigned)p[2]<<8|p[3]);
    for(unsigned i=0;i<4;i++){stamp=stamp<<8|p[4+i];ssrc=ssrc<<8|p[8+i];}
    if(!btr_ldac_allocate())return;
    struct btr_ldac_mem *m=btr_ldac;
    if(m->have_seq){
        if(m->ssrc==ssrc){
            int16_t delta=(int16_t)(seq-m->seq);if(delta<=0){btr_ldac_diag.reordered++;return;}
            if(delta!=1){m->reset_next=1;m->fragmenting=0;btr_stats.gaps++;btr_ldac_diag.sequence_gaps++;}
        }else {m->reset_next=1;m->fragmenting=m->have_stamp=0;btr_ldac_diag.sender_changes++;}
    }
    m->seq=seq;m->ssrc=ssrc;m->have_seq=1;
    if(flags&0x80){
        if(flags&0x40){
            if(m->fragmenting)m->reset_next=1;
            btr_ldac_timestamp(m,stamp,1);
            m->fragmenting=1;m->fragment_bytes=0;m->fragments_left=(uint8_t)frames;m->fragment_stamp=stamp;
        }
        if(!m->fragmenting || frames!=m->fragments_left || stamp!=m->fragment_stamp ||
           ((flags&0x20)!=0)!=(frames==1) || n-at>sizeof m->fragment-m->fragment_bytes)goto bad;
        memcpy(m->fragment+m->fragment_bytes,p+at,n-at);m->fragment_bytes+=(uint16_t)(n-at);m->fragments_left--;
        if(!(flags&0x20))return;
        m->fragmenting=0;
        if(fm1_ldac_frame_size(m->fragment,m->fragment_bytes,btr_rate)!=m->fragment_bytes)goto bad;
        btr_ldac_submit(m,m->fragment,m->fragment_bytes,1);
        m->next_stamp=stamp+FM1_LDAC_SAMPLES;m->have_stamp=1;return;
    }
    if(m->fragmenting){m->fragmenting=0;m->reset_next=1;}
    btr_ldac_timestamp(m,stamp,frames);
    btr_ldac_submit(m,p+at,n-at,frames);
    m->next_stamp=stamp+frames*FM1_LDAC_SAMPLES;m->have_stamp=1;return;
bad:
    btr_stats.malformed++;
    if(btr_ldac){btr_ldac->fragmenting=0;btr_ldac->reset_next=1;}
}
