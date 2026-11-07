/* SPDX-License-Identifier: MPL-2.0 */
/* CPU0 assembles RTP; CPU1 owns LATM parsing/decoder/PCM publication. Storage is lent by the
 * scratch allocator, after the UI releases its waveform/spectrum buffers. */
#include "aac_dec/aac_dec.h"
#define BTR_AAC_MUX 8192u
#define BTR_AAC_QUEUE_BYTES 16384u
#define BTR_AAC_QUEUE_SLOTS 16u
struct btr_aac_packet { uint32_t start,bytes;uint8_t reset,unmarked; };
struct btr_aac_mem {
    uint8_t mux[BTR_AAC_MUX], queue[BTR_AAC_QUEUE_BYTES], au[AAC_DEC_MAX_AU_BYTES];
    struct btr_aac_packet packets[BTR_AAC_QUEUE_SLOTS];
    volatile uint32_t queue_w,queue_r,packet_w,packet_r;
    int16_t pcm[AAC_DEC_PCM_SAMPLES];
    uint8_t asc[2], configured, subframes;
    uint32_t have, timestamp, ssrc, other_bits;
    uint16_t sequence;
    uint8_t sequence_valid, assembling, discard, pending;
    uint8_t active,reset_next,job_reset,job_unmarked,job_conf[6],conf[6];
    uint32_t job_bytes,job_started;
#ifdef APP_BT_RESAMPLE_CPU2
    uint32_t offsets[64],lengths[64];uint8_t au_count,au_index;
    volatile uint8_t job_done;
#endif
    const uint8_t *job_mux;
    volatile uint32_t job_result;
    void *workspace;
    aac_dec *decoder;
};
static struct btr_aac_mem *btr_aac;
static uint32_t btr_aac_max_us, btr_aac_memory;
static void btr_aac_reset(void)
{
    if(btr_aac) {
        /* Join before releasing any decoder, input, PCM or workspace storage. */
        if(btr_aac->active){plat_cpu2_wait();plat_cpu2_sync();}
        aac_dec_destroy(btr_aac->decoder);plat_work_free(btr_aac->workspace);plat_work_free(btr_aac);btr_aac=0;}
}
struct btr_bits {const uint8_t *p;uint32_t pos,end;int bad;};
static uint32_t btr_bits_get(struct btr_bits *b,uint32_t n)
{
    uint32_t v=0;
    if(n>32 || n>b->end-b->pos){b->bad=1;return 0;}
    while(n--){v=(v<<1)|((b->p[b->pos>>3]>>(7-(b->pos&7)))&1);b->pos++;}
    return v;
}
static uint32_t btr_latm_value(struct btr_bits *b)
{
    uint32_t n=1+btr_bits_get(b,2),v=0;
    while(n--)v=(v<<8)|btr_bits_get(b,8);
    return v;
}
static int btr_aac_decode_au(const uint8_t *mux,uint32_t offset,uint32_t length)
{
    struct btr_aac_mem *m=btr_aac;
        aac_dec_output out;
        struct btr_bits b={mux,offset,UINT32_MAX,0};
        for(uint32_t j=0;j<length;j++)m->au[j]=(uint8_t)btr_bits_get(&b,8);
        uint32_t started=plat_ticks24();
        int status=aac_dec_decode(m->decoder,m->au,length,m->pcm,AAC_DEC_PCM_SAMPLES,&out);
        uint32_t elapsed=(plat_ticks24()-started)/PLAT_TICKS_PER_US;if(elapsed>btr_aac_max_us)btr_aac_max_us=elapsed;
        if(status!=AAC_DEC_OK){btr_stats.decoder_errors++;return 0;}
        if(out.channels==1)for(uint32_t j=out.frames;j>0;j--)m->pcm[2*j-1]=m->pcm[2*j-2]=m->pcm[j-1];
        btr_pcm_put(m->pcm,out.frames);btr_stats.frames++;
    return 1;
}
static int btr_aac_element(const uint8_t conf[6],const uint8_t *mux,uint32_t bytes)
{
    struct btr_aac_mem *m=btr_aac;
    struct btr_bits b={mux,0,bytes*8,0};
    uint32_t offsets[64],lengths[64],count=m->subframes,other=m->other_bits;
    uint8_t asc[2]={m->asc[0],m->asc[1]};
    int fresh=!btr_bits_get(&b,1);
    if(fresh) {
        uint32_t version=btr_bits_get(&b,1),asc_end=0,object,rate,ch;
        if(version && btr_bits_get(&b,1))return 0;
        if(version)btr_latm_value(&b); /* taraBufferFullness */
        if(!btr_bits_get(&b,1))return 0; /* allStreamsSameTimeFraming */
        count=btr_bits_get(&b,6)+1;
        if(btr_bits_get(&b,4) || btr_bits_get(&b,3))return 0; /* one program/layer */
        if(version){uint32_t bits=btr_latm_value(&b);if(bits<16 || bits>b.end-b.pos)return 0;asc_end=b.pos+bits;}
        object=btr_bits_get(&b,5);rate=btr_bits_get(&b,4);ch=btr_bits_get(&b,4);
        if(object!=2 || (rate!=3 && rate!=4) || (ch!=1 && ch!=2) || btr_bits_get(&b,3))return 0;
        if((rate==3?0x80u:0x100u)!=(((uint32_t)conf[1]<<8|conf[2])&0xfff0u) ||
           (ch==1?8u:4u)!=(conf[2]&12u))return 0;
        asc[0]=(uint8_t)(16|(rate>>1));asc[1]=(uint8_t)((rate<<7)|(ch<<3));
        if(version){if(b.pos>asc_end)return 0;while(b.pos<asc_end)if(btr_bits_get(&b,1))return 0;}
        if(btr_bits_get(&b,3))return 0; /* frameLengthType=0 */
        btr_bits_get(&b,8); /* latmBufferFullness */
        other=0;
        if(btr_bits_get(&b,1)) {
            if(version)other=btr_latm_value(&b);
            else {uint32_t more;do {more=btr_bits_get(&b,1);if(other>0xffffffu)return 0;other=(other<<8)|btr_bits_get(&b,8);}while(more&&!b.bad);}
        }
        if(btr_bits_get(&b,1))btr_bits_get(&b,8); /* crcCheckSum */
    } else if(!m->configured)return 0;
    for(uint32_t i=0;i<count;i++) {
        uint32_t n=0,x;
        do{x=btr_bits_get(&b,8);n+=x;if(n>AAC_DEC_MAX_AU_BYTES || b.bad)return 0;}while(x==255);
        if(!n || n>(b.end-b.pos)/8)return 0;
        offsets[i]=b.pos;lengths[i]=n;b.pos+=n*8;
    }
    if(b.bad || other>b.end-b.pos)return 0;
    b.pos+=other;
    if(b.end-b.pos>7)return 0;
    while(b.pos<b.end)if(btr_bits_get(&b,1))return 0;
    if(b.bad)return 0;
    if(fresh && (!m->configured || !m->decoder || memcmp(asc,m->asc,2))) {
        void *workspace=m->workspace;
        aac_dec_destroy(m->decoder);m->decoder=0;m->configured=0;
        if(aac_dec_init(workspace,aac_dec_workspace_size(),asc,2,&m->decoder)!=AAC_DEC_OK)return 0;
    }
    memcpy(m->asc,asc,2);m->configured=1;m->subframes=(uint8_t)count;m->other_bits=other;
#ifdef APP_BT_RESAMPLE_CPU2
    memcpy(m->offsets,offsets,count*sizeof *offsets);memcpy(m->lengths,lengths,count*sizeof *lengths);
    m->au_count=(uint8_t)count;m->au_index=0;
#else
    for(uint32_t i=0;i<count;i++)if(!btr_aac_decode_au(mux,offsets[i],lengths[i]))return 0;
#endif
    return 1;
}
/* Single worker, bounded compressed-byte queue; no CPU0 decode
 * fallback and no allocator/UI/USB/radio calls on the worker. */
static void btr_aac_job(uint32_t unused)
{
    struct btr_aac_mem *m=btr_aac;(void)unused;
#ifdef APP_BT_RESAMPLE_CPU2
    int result=1;
    if(!m->au_count) {
        if(m->job_reset&2u)m->configured=0;
        if((m->job_reset&1u) && m->decoder)aac_dec_reset(m->decoder);
        result=btr_aac_element(m->job_conf,m->job_mux,m->job_bytes);
    }
    if(result) {
        unsigned i=m->au_index;
        result=btr_aac_decode_au(m->job_mux,m->offsets[i],m->lengths[i]);
        if(result && ++m->au_index<m->au_count)result=2; /* validated element retained */
    }
    if(result!=2)m->au_count=0;
    if(!result && m->decoder)aac_dec_reset(m->decoder);
    plat_cpu2_sync();m->job_result=(uint32_t)result;m->job_done=1;
#else
    if(m->job_reset&2u)m->configured=0;
    if((m->job_reset&1u) && m->decoder)aac_dec_reset(m->decoder);
    int result=btr_aac_element(m->job_conf,m->job_mux,m->job_bytes);
    if(!result && m->decoder)aac_dec_reset(m->decoder);
    plat_cpu2_sync();m->job_result=(uint32_t)result;
#endif
}
static void btr_aac_poll_inner(void)
{
    struct btr_aac_mem *m=btr_aac;if(!m)return;
#ifdef APP_BT_RESAMPLE_CPU2
    btr_resample_poll();
#endif
    m->pending=m->packet_w-m->packet_r>(uint32_t)m->active;
    if(m->active){
#ifdef APP_BT_RESAMPLE_CPU2
        if(!m->job_done)return;
        plat_cpu2_sync();m->active=0;
        if(m->job_result!=2) {
#else
        if(plat_cpu2_busy()){
            /* A stuck decoder must still reach the platform's crash guard. */
            if(plat_ticks24()-m->job_started<2000000u*PLAT_TICKS_PER_US)return;
        }
        plat_cpu2_wait();plat_cpu2_sync();m->active=0;
#endif
        struct btr_aac_packet *done=&m->packets[m->packet_r%BTR_AAC_QUEUE_SLOTS];
        m->queue_r=done->start+done->bytes;m->packet_r++;
        if(!m->job_result){
            if(m->job_unmarked)btr_stats.gaps++;else btr_stats.malformed++;
        }
#ifdef APP_BT_RESAMPLE_CPU2
        }
#endif
    }
    m->pending=m->packet_w!=m->packet_r;
    if(!m->pending)return;
#ifdef APP_BT_RESAMPLE_CPU2
    if(!m->workspace)return; /* allocated only by the foreground wrapper */
#else
    if(!m->workspace){
        m->workspace=plat_work_alloc((uint32_t)aac_dec_workspace_size());
        if(!m->workspace)return; /* retain first StreamMuxConfig */
        btr_aac_memory=((sizeof *m+7u)&~7u)+(uint32_t)aac_dec_workspace_size();
    }
#endif
    struct btr_aac_packet *next=&m->packets[m->packet_r%BTR_AAC_QUEUE_SLOTS];
#ifdef APP_BT_RESAMPLE_CPU2
    if(btr_pcm_space()<1024u)return;
    if(!m->au_count) {
#endif
    m->job_bytes=next->bytes;m->job_mux=m->queue+(next->start&(BTR_AAC_QUEUE_BYTES-1u));
    memcpy(m->job_conf,m->conf,6);m->job_unmarked=next->unmarked;
    m->job_reset=next->reset;m->job_started=plat_ticks24();
#ifdef APP_BT_RESAMPLE_CPU2
    }
    m->job_done=0;
#endif
    m->active=1;plat_cpu2_sync();
#ifdef APP_BT_RESAMPLE_CPU2
    if(!btr_worker_run(btr_aac_job,0)){
#else
    if(!plat_cpu2_run(btr_aac_job,0)){
#endif
        m->active=0;
#ifndef APP_BT_RESAMPLE_CPU2
        btr_stats.decoder_errors++;
#endif
        return;
    }
    m->pending=m->packet_w-m->packet_r>1u;
}
static void btr_aac_poll(void)
{
#ifdef APP_BT_RESAMPLE_CPU2
    struct btr_aac_mem *m=btr_aac;
    if(m && m->packet_r!=m->packet_w && !m->workspace) {
        void *p=plat_work_alloc((uint32_t)aac_dec_workspace_size());
        if(p){plat_cpu2_sync();m->workspace=p;btr_aac_memory=((sizeof *m+7u)&~7u)+(uint32_t)aac_dec_workspace_size();}
    }
    uint32_t lock=plat_lock();btr_aac_poll_inner();plat_unlock(lock);
#else
    btr_aac_poll_inner();
#endif
}
static void btr_aac_queue(const uint8_t conf[6],int unmarked)
{
    struct btr_aac_mem *m=btr_aac;
    uint32_t at=m->queue_w&(BTR_AAC_QUEUE_BYTES-1u);
    uint32_t skip=m->have>BTR_AAC_QUEUE_BYTES-at?BTR_AAC_QUEUE_BYTES-at:0;
    uint32_t need=skip+m->have;
    if(m->packet_w-m->packet_r==BTR_AAC_QUEUE_SLOTS ||
       need>BTR_AAC_QUEUE_BYTES-(m->queue_w-m->queue_r)){
        btr_stats.gaps++;m->reset_next|=1u;
    }else{
        struct btr_aac_packet *next=&m->packets[m->packet_w%BTR_AAC_QUEUE_SLOTS];
        next->start=m->queue_w+skip;next->bytes=m->have;
        next->reset=m->reset_next;next->unmarked=(uint8_t)unmarked;m->reset_next=0;
        memcpy(m->queue+(next->start&(BTR_AAC_QUEUE_BYTES-1u)),m->mux,m->have);
#ifdef APP_BT_RESAMPLE_CPU2
        uint32_t lock=plat_lock();plat_cpu2_sync();
#endif
        memcpy(m->conf,conf,6);m->queue_w=next->start+m->have;m->packet_w++;
#ifdef APP_BT_RESAMPLE_CPU2
        plat_unlock(lock);
#endif
    }
    m->have=0;m->assembling=0;btr_aac_poll();
}

static uint32_t btr_be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void btr_aac_media(const uint8_t *p,uint32_t n,const uint8_t conf[6])
{
    uint32_t at,ts,ssrc;uint16_t seq;
    btr_stats.packets++;
    if(!btr_aac) {
        uint32_t bytes=(sizeof(struct btr_aac_mem)+7u)&~7u;
        btr_aac=plat_work_alloc(bytes);if(!btr_aac){btr_stats.decoder_errors++;return;}
        memset(btr_aac,0,sizeof *btr_aac);if(bytes>btr_aac_memory)btr_aac_memory=bytes;
    }
    struct btr_aac_mem *m=btr_aac;
    btr_aac_poll();
    if(n<13 || p[0]>>6!=2)goto bad;
    at=12+4*(p[0]&15);if(at>n)goto bad;
    if(p[0]&16){if(n-at<4)goto bad;uint32_t ext=((uint32_t)p[at+2]<<8|p[at+3])*4;if(ext>n-at-4)goto bad;at+=4+ext;}
    if(p[0]&32){uint32_t pad=p[n-1];if(!pad || pad>n-at)goto bad;n-=pad;}
    if(at>=n)goto bad;
    seq=(uint16_t)((uint16_t)p[2]<<8|p[3]);ts=btr_be32(p+4);ssrc=btr_be32(p+8);
    if(m->sequence_valid) {
        int16_t delta=(int16_t)(seq-m->sequence);
        if(ssrc==m->ssrc && delta<=0)return;
        if(delta!=1 || ssrc!=m->ssrc){
            int lost_fragment=m->assembling && ts==m->timestamp && ssrc==m->ssrc;
            btr_stats.gaps++;m->have=0;
            m->reset_next|=ssrc!=m->ssrc?3u:1u;
            if(lost_fragment){m->sequence=seq;m->discard=!(p[1]&128);m->assembling=0;return;}
            m->assembling=0;
        }
    }
    m->sequence_valid=1;m->sequence=seq;m->ssrc=ssrc;
    if(m->discard){if(ts==m->timestamp){if(p[1]&128)m->discard=0;return;}m->discard=0;}
    if(m->assembling && m->timestamp!=ts){
        /* Some sources omit M even on the final fragment. A new timestamp
         * closes the previous element, but only the full bounded LATM parser
         * may publish it. Incomplete/lost elements still produce no PCM. */
        btr_aac_queue(conf,1);
    }
    if(!m->assembling){m->have=0;m->timestamp=ts;}
    if(n-at>BTR_AAC_MUX-m->have)goto bad;
    memcpy(m->mux+m->have,p+at,n-at);m->have+=n-at;m->assembling=1;
    if(!(p[1]&128))return;
    btr_aac_queue(conf,0);return;
bad:
    btr_stats.malformed++;m->assembling=0;m->have=0;
    m->reset_next|=1u;
}
