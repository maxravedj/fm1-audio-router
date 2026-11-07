/* SPDX-License-Identifier: MPL-2.0 */
/* Bounded protocol metadata only: never addresses, link keys, nonces or PCM.
 * Main owns traces/faults. The TX handover hook only counts/stores integers. */
#define BR_TRACE_N 16u
struct br_trace { uint32_t ms;uint16_t op;uint8_t tid,before,after; };
static struct {
    volatile uint32_t sent,last_tx;
    uint32_t trace_w,last_op;
    struct br_trace trace[BR_TRACE_N];
} brd;
static struct {
    uint32_t valid,reason,detail,phase,sec,at,age,gap,good,bad,syncs,carries,bit,offset,sent,pending,last_tx,last_op,rxbit,status;
    uint32_t trace_n;
    struct br_trace trace[BR_TRACE_N];
} br_fault;
enum { BR_FAIL_RX=1,BR_FAIL_AUTH,BR_FAIL_SSP,BR_FAIL_REMOTE,BR_FAIL_USER,BR_FAIL_NOT_PAIRING,BR_FAIL_PROFILE };
static void br_note_tx(const uint8_t *p,uint32_t n,uint32_t llid)
{
    uint32_t tid;
    if(llid!=3)return;
    brd.last_tx=btl_opcode(p,n,&tid);brd.sent++;
}
static void br_trace_note(uint32_t op,uint32_t tid,uint32_t before,uint32_t after)
{
    struct br_trace *t=&brd.trace[brd.trace_w++%BR_TRACE_N];
    t->ms=fm1_ms-brl.started;t->op=(uint16_t)op;t->tid=(uint8_t)tid;
    t->before=(uint8_t)before;t->after=(uint8_t)after;brd.last_op=op;
}
static void br_capture_fault(uint32_t reason,uint32_t detail)
{
    uint32_t f=irq_save();
    br_fault.valid=1;br_fault.reason=reason;br_fault.detail=detail;br_fault.phase=brl.phase;br_fault.sec=brl_ssp.st;
    br_fault.at=fm1_ms;br_fault.age=fm1_ms-brl.started;br_fault.gap=fm1_ms-brl.last_good;
    br_fault.good=brl.good;br_fault.bad=brl.bad;br_fault.syncs=brl.syncs;br_fault.carries=brl.carries;
    br_fault.bit=brl.bit;br_fault.offset=brl.offset;br_fault.sent=brd.sent;br_fault.pending=blt.w-blt.r;
    br_fault.last_tx=brd.last_tx;br_fault.last_op=brd.last_op;br_fault.rxbit=brl.rxbit;br_fault.status=brl.status;
    irq_restore(f);
    br_fault.trace_n=brd.trace_w<BR_TRACE_N?brd.trace_w:BR_TRACE_N;
    for(uint32_t i=0;i<br_fault.trace_n;i++)br_fault.trace[i]=brd.trace[(brd.trace_w-br_fault.trace_n+i)%BR_TRACE_N];
}
/* Fits the existing USB log's 43-character payload. Each row has named numeric
 * fields documented in BT-RECEIVER-PAGE.md; retry scans cannot overwrite it. */
static char *br_debug_numbers(char *p,uint32_t a,uint32_t b,uint32_t c)
{
    p=put_dec(p,a);*p++=' ';p=put_dec(p,b);*p++=' ';p=put_dec(p,c);*p=0;return p;
}
static const char *br_debug_line(uint32_t row)
{
    static char text[48];const char *label=0;uint32_t a=0,b=0,c=0;
    switch(row) {
    case 0:label="BT now ";a=brl.phase;b=brl_ssp.st;c=brl.encrypted;break;
    case 1:label="BT rx ";a=brl.good;b=brl.bad;c=brl.syncs;break;
    case 2:label="BT tx ";a=brd.sent;b=blt.w-blt.r;c=brd.last_tx;break;
    case 3:label="BT fail ";a=br_fault.valid?br_fault.reason:0;b=br_fault.detail;break;
    case 4:label="BT stage ";a=br_fault.phase;b=br_fault.sec;c=br_fault.at;break;
    case 5:label="BT age ";a=br_fault.age;b=br_fault.gap;break;
    case 6:label="BT frx ";a=br_fault.good;b=br_fault.bad;c=br_fault.syncs;break;
    case 7:label="BT ftx ";a=br_fault.sent;b=br_fault.pending;c=br_fault.last_tx;break;
    case 8:label="BT sync ";a=br_fault.bit;b=br_fault.offset;c=br_fault.carries;break;
    case 9:label="BT last ";a=br_fault.rxbit;b=br_fault.status;c=br_fault.last_op;break;
    case 10:label="BT profile ";a=brc.profile.state;
        for(uint32_t j=0;j<3;j++)b|=(uint32_t)(brc.ch[j].cfg_in|brc.ch[j].cfg_out<<1)<<(2*j);
        c=ba_live;break;
    case 11:label="BT AVDTP ";a=brc.signal;b=brc.reply;c=brc.error;break;
    case 12:label="BT PSM ";a=brc.last_psm;b=brc.conn_result;c=brl.reconnect_hold;break;
    case 13:label="BT config ";a=brc.conf_cid;b=brc.conf_result;c=brc.conf_option;break;
    default: {
        uint32_t i=row-14;
#ifdef APP_BT_AAC
        if(i<6) {
            switch(i) {
            case 0:label="BT codec ";a=brc.profile.codec;b=btr_rate;c=brc.profile.codec==BTR_CODEC_AAC?((brc.profile.conf[2]&8)?1:2):2;break;
            case 1:label="BT audio ";a=btr_stats.packets;b=btr_stats.frames;c=btr_stats.decoder_errors;break;
            case 2:label="BT queue ";a=btr_stats.underruns;
#ifdef APP_BT_RESAMPLE_CPU2
                a+=btr_output_underruns;
#endif
                b=btr_stats.overruns;c=btr_stats.gaps;break;
            case 3:label="BT AAC us/mem ";a=btr_aac_max_us;b=btr_aac_memory;c=btr_aac!=0;break;
            case 4:label="BT malformed ";a=btr_stats.malformed;break;
            case 5:label="BT AAC cfg ";
                if(brc.profile.codec==BTR_CODEC_AAC){a=brc.profile.conf[0];b=brc.profile.conf[3]>>7;c=((uint32_t)(brc.profile.conf[3]&127)<<16)|((uint32_t)brc.profile.conf[4]<<8)|brc.profile.conf[5];}
                break;
            default:return 0;
            }
            break;
        }
        i-=6;
#endif
        if(i>=br_fault.trace_n)return 0;
        const struct br_trace *t=&br_fault.trace[i];char *p=text;
        const char *s="BT trace ";while(*s)*p++=*s++;
        p=put_dec(p,t->ms);*p++=' ';p=put_dec(p,t->op);*p++='/';p=put_dec(p,t->tid);
        *p++=' ';p=put_dec(p,t->before);*p++='>';p=put_dec(p,t->after);*p=0;
        return text;
    }
    }
    char *p=text;while(*label)*p++=*label++;
    br_debug_numbers(p,a,b,c);return text;
}
