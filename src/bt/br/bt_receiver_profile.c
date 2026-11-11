/* SPDX-License-Identifier: MPL-2.0 */
#include "bt_receiver.h"
static int btr_one(uint8_t x) { return x && !(x & (x-1u)); }
static int btr_conf_valid(const uint8_t c[4])
{
    return btr_one(c[0]&0xf0) && !(c[0]&0xc0) && btr_one(c[0]&15) &&
        btr_one(c[1]&0xf0) && btr_one(c[1]&12) && btr_one(c[1]&3) &&
        c[2]>=2 && c[2]<=c[3] && c[3]<=250;
}
static int btr_aac_conf_valid(const uint8_t c[6])
{
    return (c[0]==0x80 || c[0]==0x40) &&
        ((c[1]==1 && !(c[2]&0xf0)) || (!c[1] && (c[2]&0xf0)==0x80)) &&
        ((c[2]&15)==4 || (c[2]&15)==8);
}
/* Stable bits: SBC, AAC, aptX, aptX HD, LDAC. Empty masks are rejected. */
static uint8_t btr_codec_mask=3;
static unsigned btr_codec_for_seid(unsigned seid) { return seid==4?BTR_CODEC_SBC:seid==8?BTR_CODEC_AAC:seid==12?BTR_CODEC_APTX:seid==16?BTR_CODEC_APTX_HD:seid==20?BTR_CODEC_LDAC:0; }
static unsigned btr_codec_len(unsigned codec) { return codec==BTR_CODEC_AAC?6:codec==BTR_CODEC_APTX?7:codec==BTR_CODEC_APTX_HD?11:codec==BTR_CODEC_LDAC?8:4; }
static unsigned btr_codec_type(unsigned codec) { return codec==BTR_CODEC_AAC?2:codec>=BTR_CODEC_APTX?255:0; }
static int btr_seid(uint32_t seid)
{
    unsigned codec=btr_codec_for_seid(seid);
#ifndef APP_BT_AAC
    if(codec==BTR_CODEC_AAC)return 0;
#endif
#ifndef APP_BT_APTX
    if(codec==BTR_CODEC_APTX || codec==BTR_CODEC_APTX_HD)return 0;
#endif
#ifndef APP_BT_LDAC
    if(codec==BTR_CODEC_LDAC)return 0;
#endif
    return codec && (btr_codec_mask & (1u<<(codec-1)));
}
static int btr_aptx_conf_valid(const uint8_t *c,unsigned hd)
{
    const uint8_t vendor[6]={hd?0xd7:0x4f,0,0,0,hd?0x24:1,0};
    for(unsigned i=0;i<6;i++)if(c[i]!=vendor[i])return 0;
    if(c[6]!=0x12 && c[6]!=0x22)return 0; /* 48/44.1k, stereo only */
    if(hd)for(unsigned i=7;i<11;i++)if(c[i])return 0;
    return 1;
}
static unsigned btr_config_rate(const struct btr_profile *s)
{
    if(s->codec==BTR_CODEC_LDAC || s->codec==BTR_CODEC_APTX || s->codec==BTR_CODEC_APTX_HD)return s->conf[6]&0x10?48000:44100;
    return s->codec==BTR_CODEC_AAC?(s->conf[2]&0x80?48000:44100):(s->conf[0]&0x10?48000:44100);
}
static int btr_ldac_conf_valid(const uint8_t *c)
{
    static const uint8_t id[]={0x2d,1,0,0,0xaa,0};
    for(unsigned i=0;i<6;i++)if(c[i]!=id[i])return 0;
    return (c[6]==0x20 || c[6]==0x10) && c[7]==1;
}
uint32_t btr_signal(struct btr_profile *s, const uint8_t *p, uint32_t n, uint8_t out[64])
{
    uint32_t sig, k=2, i, cat=0, error=0, at, seen=0;
    uint8_t conf[11]={0};
    uint32_t seid=n>2?p[2]:0, codec=btr_codec_for_seid(seid), clen=btr_codec_len(codec);
    if(n<2 || (p[0]&3)) return 0; /* commands only */
    out[0]=(p[0]&0xf0)|2;out[1]=sig=p[1]&63;
    if((p[0]&12) || (p[1]&0xc0)) { out[0]=(p[0]&0xf0)|1;return 2; } /* general reject: fragmented signalling unsupported */
    if(sig==1) { if(n!=2) error=0x11;else for(unsigned ep=4;ep<=20;ep+=4)if(btr_seid(ep)){out[k++]=(uint8_t)(ep|(s->state!=BTR_IDLE?2:0));out[k++]=8;} }
    else if(sig==2 || sig==12) {
        if(n!=3 || !btr_seid(seid)) error=0x12;
        else {
            const uint8_t caps[]={1,0,7,6,0,0,BTR_CAPS[0],BTR_CAPS[1],2,250};
            const uint8_t aac_caps[]={1,0,7,8,0,2,0xc0,1,0x8c,0x80,0,0};
            const uint8_t aptx_caps[]={1,0,7,9,0,255,0x4f,0,0,0,1,0,0x32};
            const uint8_t hd_caps[]={1,0,7,13,0,255,0xd7,0,0,0,0x24,0,0x32,0,0,0,0};
            const uint8_t ldac_caps[]={1,0,7,10,0,255,0x2d,1,0,0,0xaa,0,0x30,1};
            const uint8_t *c=codec==BTR_CODEC_LDAC?ldac_caps:codec==BTR_CODEC_AAC?aac_caps:codec==BTR_CODEC_APTX?aptx_caps:codec==BTR_CODEC_APTX_HD?hd_caps:caps;
            for(i=0;i<clen+6;i++)out[k++]=c[i];}
    } else if(sig==3 || sig==5) { /* SET_CONFIGURATION / RECONFIGURE */
        if(n<(sig==3?4u:3u) || !btr_seid(seid)) error=0x12;
        else if(sig==3 ? s->state!=BTR_IDLE : (s->state!=BTR_OPEN || s->seid!=seid)) error=0x31;
        else if(sig==3 && (!(p[3]>>2) || (p[3]&3))) error=0x12;
        else {
            at=sig==3?4:3;
            while(at<n && !error) {
                cat=p[at++];if(at>=n){error=0x11;break;}
                i=p[at++];if(i>n-at){error=0x11;break;}
                if(cat==1 && sig==3 && !i && !(seen&1))seen|=1;
                else if(cat==7 && i==clen+2 && !(seen&2) && !p[at] && p[at+1]==btr_codec_type(codec)) {
                    for(k=0;k<clen;k++)conf[k]=p[at+2+k];
                    if(!(codec==BTR_CODEC_LDAC?btr_ldac_conf_valid(conf):codec==BTR_CODEC_AAC?btr_aac_conf_valid(conf):(codec==BTR_CODEC_APTX || codec==BTR_CODEC_APTX_HD)?btr_aptx_conf_valid(conf,codec==BTR_CODEC_APTX_HD):btr_conf_valid(conf)))error=0x29;else seen|=2;
                } else error=cat==7?0x29:0x17;
                at+=i;
            }
            if(!error && seen!=(sig==3?3u:2u)) {cat=7;error=0x11;}
            if(!error) {for(i=0;i<11;i++)s->conf[i]=conf[i];s->codec=(uint8_t)codec;s->seid=(uint8_t)seid;if(sig==3)s->state=BTR_CONFIGURED;}
        }
        k=2;
    } else if(sig==4) { /* GET_CONFIGURATION */
        if(n!=3 || !btr_seid(seid))error=0x12;
        else if(s->state==BTR_IDLE || s->seid!=seid)error=0x31;
        else {out[k++]=1;out[k++]=0;out[k++]=7;out[k++]=(uint8_t)(clen+2);out[k++]=0;out[k++]=btr_codec_type(codec);
            for(i=0;i<clen;i++)out[k++]=s->conf[i];}
    } else if(sig>=6 && sig<=10) {
        if(n!=3 || !btr_seid(seid))error=0x12;
        else if(s->state!=BTR_IDLE && s->seid!=seid)error=0x12;
        else if(sig==6 && s->state==BTR_CONFIGURED)s->state=BTR_OPEN;
        else if(sig==7 && s->state==BTR_OPEN && s->media)s->state=BTR_STREAMING;
        else if(sig==9 && s->state==BTR_STREAMING)s->state=BTR_OPEN;
        else if(sig==8 && (s->state==BTR_OPEN || s->state==BTR_STREAMING))s->state=BTR_IDLE;
        else if(sig==10)s->state=BTR_IDLE;
        else error=0x31;
    } else {out[0]=(p[0]&0xf0)|1;return 2;}
    if(error) {
        out[0]=(p[0]&0xf0)|3;k=2;
        if(sig==3 || sig==5)out[k++]=(uint8_t)cat;
        if(sig==7 || sig==9)out[k++]=(uint8_t)seid;
        out[k++]=(uint8_t)error;
    }
    return k;
}
uint32_t btr_frame_size(const uint8_t *p,uint32_t n,const uint8_t c[4])
{
    uint32_t sb,blocks,mode,ch,len,bits;
    if(n<4 || p[0]!=0x9c || (p[1]>>6)<2) return 0;
    sb=(p[1]&1)?8:4;blocks=4*(1+((p[1]>>4)&3));mode=(p[1]>>2)&3;ch=mode?2:1;
    if(!(c[0] & (0x80u>>(p[1]>>6))) || !(c[0]&(8u>>mode)) ||
       !(c[1]&(0x80u>>((p[1]>>4)&3))) || !(c[1]&(sb==8?4:8)) ||
       !(c[1]&((p[1]&2)?2:1)) || p[2]<c[2] || p[2]>c[3] ||
       p[2]>(mode<2?16u:32u)*sb) return 0;
    bits=blocks*p[2]*(mode==1?2:1)+(mode==3?sb:0);
    len=4+sb*ch/2+(bits+7)/8;
    return len<=n?len:0;
}
