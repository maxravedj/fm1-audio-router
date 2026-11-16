/* SPDX-License-Identifier: MPL-2.0 */
/* Incoming basic-mode L2CAP, SDP Audio Sink and AVDTP. Main thread only.
 * BTR_SEND(frame,len) publishes a whole response atomically to the link queue.
 * BTR_FLUSH(rate) clears decoder/PCM with the audio consumer excluded. */
/* BTR_SECURE() is the link's successful encryption state. SDP/control are
 * permitted without it; AVDTP signalling and media always require it. */
#include "bt_l2cap.c"
#include "bt_sdps.c"
#include "bt_receiver_profile.c"
#define BTR_MTU 1008u
#ifndef BTR_ROOM
#define BTR_ROOM(slots) 1
#endif
#ifndef BTR_OWNS_AUDIO
#define BTR_OWNS_AUDIO() 1
#endif
#ifndef BTR_PROFILE_HOLD
#define BTR_PROFILE_HOLD(hold) ((void)0)
#endif
#ifdef APP_BT_MULTIPOINT
#define BTR_CHANNELS 4u
#else
#define BTR_CHANNELS 3u
#endif
struct btr_channel { uint16_t remote,mtu;uint8_t cfg_in,cfg_out,id; };
static struct { struct btr_channel ch[BTR_CHANNELS];uint8_t rx[BTR_MTU+4];uint32_t have,need,signal_at;uint8_t pending,id,connect_id,connect_sent,profile_ready,avrcp_id,avrcp_sent,pause,label;uint16_t last_psm;uint8_t conn_result,conf_cid,conf_result,conf_option,signal,reply,error;struct btr_profile profile; } br_channels[
#ifdef APP_BT_MULTIPOINT
BR_LINKS
#else
1
#endif
];
#ifdef APP_BT_MULTIPOINT
#define brc (br_channels[br_current])
#else
#define brc (br_channels[0])
#endif
static uint32_t br_le16(const uint8_t *p) {return p[0]|(uint32_t)p[1]<<8;}
static void br_send(uint32_t channel,const uint8_t *p,uint32_t n)
{
    uint8_t out[BSD_OUT+4];uint32_t i;
    if(channel>=BTR_CHANNELS || !brc.ch[channel].remote || n>BSD_OUT || n>brc.ch[channel].mtu)return;
    out[0]=(uint8_t)n;out[1]=(uint8_t)(n>>8);out[2]=(uint8_t)brc.ch[channel].remote;out[3]=(uint8_t)(brc.ch[channel].remote>>8);
    for(i=0;i<n;i++)out[4+i]=p[i];BTR_SEND(out,n+4);
}
static void br_reset(void) {
    memset(&brc,0,sizeof brc);
    if(BTR_OWNS_AUDIO()){
        BTR_FLUSH(44100);ba_live=0;
#ifdef APP_BT_MULTIPOINT
        br_audio_link=255;
#endif
    }
}
static void br_configure(uint32_t c)
{
    uint8_t out[20];uint32_t n;
    brc.ch[c].id=++brc.id;if(!brc.id)brc.ch[c].id=++brc.id;
    n=bl_conf_req(out,brc.ch[c].id,brc.ch[c].remote,BTR_MTU);BTR_SEND(out,n);
}
/* An outgoing bonded ACL alone does not connect the audio profile. Open the
 * signalling channel; Android's A2DP pending handler then negotiates the
 * Source stream with our advertised Sink endpoints. Never send before E0. */
static void br_connect_signal(void)
{
    uint8_t out[12];
    if(!BTR_SECURE() || brc.ch[1].remote || brc.connect_sent)return;
    uint8_t id=(uint8_t)(brc.id+1u);if(!id)id=1;
    /* Failed admission has no side effects: the normal poll retries. */
    if(BTR_SEND(out,bl_conn_req(out,id,0x19,65))) {
        brc.id=brc.connect_id=id;brc.connect_sent=1;
    }
}
static __attribute__((noinline)) uint32_t br_sig(const uint8_t *p,uint32_t n)
{
    uint8_t out[32];uint32_t at=0,len,code,id,c,local,remote,psm,result,k,mtu;
    while(n-at>=4) {
        /* A compound signalling SDU can contain more replies than fit in the
         * whole queue. Retain its unprocessed suffix, rather than replaying
         * commands whose replies and state changes have already committed. */
        if(!BTR_ROOM(4))return at;
        code=p[at];id=p[at+1];len=br_le16(p+at+2);at+=4;if(len>n-at)return n;
        const uint8_t *d=p+at;at+=len;if(!id)continue;
        if(code==BL_CONN_REQ && len==4) {
            psm=br_le16(d);remote=br_le16(d+2);c=BTR_CHANNELS;
            if(psm==1)c=0;
#ifdef APP_BT_MULTIPOINT
            else if(psm==0x17)c=3;
#endif
            else if(psm==0x19)c=!brc.ch[1].remote?1:brc.profile.state==BTR_OPEN&&!brc.ch[2].remote?2:BTR_CHANNELS;
            result=c==BTR_CHANNELS?2:remote<64||brc.ch[c].remote?4:0;
            if(c==1 && brc.connect_id)result=4; /* one signalling channel during collision */
            for(k=0;k<BTR_CHANNELS;k++)if(brc.ch[k].remote==remote)result=4;
            if((psm==0x19 || psm==0x17) && !BTR_SECURE())result=3; /* security block */
            brc.last_psm=(uint16_t)psm;brc.conn_result=(uint8_t)result;
            BTR_SEND(out,bl_conn_rsp(out,id,result?0:64+c,remote,result));
            if(!result){
#ifdef APP_BT_MULTIPOINT
                if(c==3)brc.avrcp_id=0;
                if(c==1)brc.avrcp_sent=0;
#endif
                /* An incoming encrypted signalling open is allowed even
                 * after the user closed this profile on the existing ACL. */
                if(c==1)BTR_PROFILE_HOLD(0);
                brc.ch[c].remote=(uint16_t)remote;brc.ch[c].mtu=672;br_configure(c);}
        } else if(code==BL_CONN_RSP && len==8 && (id==brc.connect_id
#ifdef APP_BT_MULTIPOINT
            || id==brc.avrcp_id
#endif
            )) {
            remote=br_le16(d);local=br_le16(d+2);result=br_le16(d+4);
            c=1;
#ifdef APP_BT_MULTIPOINT
            if(id==brc.avrcp_id)c=3;
#endif
            if(local!=64+c || result==1)continue; /* pending: same transaction */
            if(c==1)brc.connect_id=0;else brc.avrcp_id=0;
            if(result || remote<64 || br_le16(d+6) || brc.ch[c].remote)continue;
            for(k=0;k<BTR_CHANNELS;k++)if(brc.ch[k].remote==remote)break;
            if(k<BTR_CHANNELS)continue;
            brc.ch[c].remote=(uint16_t)remote;brc.ch[c].mtu=672;br_configure(c);
        } else if(code==BL_CONF_REQ && len>=4) {
            local=br_le16(d);if(local<64||local>=64+BTR_CHANNELS)continue;c=local-64;
            if(!brc.ch[c].remote)continue;
            result=br_le16(d+2)?2:0;mtu=brc.ch[c].cfg_in?brc.ch[c].mtu:672;
            uint32_t options=14;uint8_t bad_option=0;
            for(k=4;!br_le16(d+2) && k<len;) {
                if(k+2>len || d[k+1]>len-k-2){result=2;break;}
                uint32_t type=d[k]&127,size=d[k+1]+2u;
                if(type==1 && size==4)mtu=br_le16(d+k+2);
                /* Flush Timeout describes the peer's transmitter, not ours.
                 * An explicit Basic-mode RFC option adds no framing. Core
                 * Vol 3 A sections 5.2/5.4: neither is an unknown option. */
                else if(type==2 && size==4) {
                    if(!br_le16(d+k+2)){result=2;bad_option=2;break;}
                }
                else if(type==4 && size==11) {
                    if(d[k+2] && result!=3){result=1;bad_option=4;}
                }
                else if(!(d[k]&128)) {
                    bad_option=(uint8_t)type;
                    if(options+size>sizeof out){result=2;break;}
                    memcpy(out+options,d+k,size);options+=size;result=3;
                }
                k+=size;
            }
            if(result==1) {
                options=25;out[14]=4;out[15]=9;memset(out+16,0,9);
            }
            if(mtu<48 && !result) {
                result=1;bad_option=1;options=18;
                out[14]=1;out[15]=2;bl_le16(out+16,48);
            }
            brc.conf_cid=(uint8_t)local;brc.conf_result=(uint8_t)result;brc.conf_option=bad_option;
            uint32_t bytes=bl_conf_rsp(out,id,brc.ch[c].remote,result);
            if(result==1 || result==3)bytes=bl_sig(out,BL_CONF_RSP,id,options-8);
            BTR_SEND(out,bytes);
            if(!result){brc.ch[c].cfg_in=1;brc.ch[c].mtu=(uint16_t)mtu;}
        } else if(code==BL_CONF_RSP && len>=6) {
            local=br_le16(d);if(local<64||local>=64+BTR_CHANNELS)continue;c=local-64;
            if(brc.ch[c].id==id && !br_le16(d+2) && !br_le16(d+4))brc.ch[c].cfg_out=1;
        } else if(code==BL_DISC_REQ && len==4) {
            local=br_le16(d);remote=br_le16(d+2);
            if(local<64||local>=64+BTR_CHANNELS)continue;c=local-64;if(remote!=brc.ch[c].remote)continue;
            BTR_SEND(out,bl_disc_rsp(out,id,local,remote));memset(&brc.ch[c],0,sizeof brc.ch[c]);
#ifdef APP_BT_MULTIPOINT
            /* Do not immediately reopen a peer-closed AVRCP connection: many
             * hosts bring A2DP back with it during a deliberate Disconnect. */
            if(c==3){brc.avrcp_sent=1;brc.avrcp_id=0;}
#endif
            if(c==1 && brc.profile_ready)BTR_PROFILE_HOLD(1);
            if(c==1 || c==2){
                brc.profile.state=c==1?BTR_IDLE:BTR_CONFIGURED;brc.profile.media=0;if(BTR_OWNS_AUDIO())ba_live=0;
                BTR_FLUSH(btr_config_rate(&brc.profile));
#if defined(APP_BT_LDAC) && !defined(PLAT_HOST)
                if(BTR_OWNS_AUDIO() && c==2 && brc.profile.codec==BTR_CODEC_LDAC)btr_ldac_allocate();
#endif
            }
        } else if(code==BL_INFO_REQ && len==2) {
            uint32_t type=br_le16(d),bytes=type==2?8:4;
            /* Basic mode only: no ERTM, fixed channels or connectionless service. */
            bl_le16(out+8,type);bl_le16(out+10,type==2?0:1);
            memset(out+12,0,4);BTR_SEND(out,bl_sig(out,BL_INFO_RSP,id,bytes));
        } else if(code==8) { /* Echo */
            if(len<=24){memcpy(out+8,d,len);BTR_SEND(out,bl_sig(out,9,id,len));}
        } else if(code!=BL_CMD_REJECT) {
            out[0]=6;out[1]=0;out[2]=1;out[3]=0;out[4]=1;out[5]=(uint8_t)id;out[6]=2;out[7]=0;out[8]=out[9]=0;BTR_SEND(out,10);
        }
    }
    return n;
}
/* Media-only leaf: no signalling, graph allocation callback or control retry.
 * The drawing checkpoint calls this through br_media_acl, so a retained
 * control frame cannot create a display -> control -> display recursion. */
static void br_media_frame(const uint8_t *p,uint32_t n)
{
    if(!BTR_SECURE() || !BTR_OWNS_AUDIO() || brc.profile.state!=BTR_STREAMING ||
       !brc.ch[2].cfg_in || !brc.ch[2].cfg_out)return;

#if defined(APP_BT_LDAC) && !defined(PLAT_HOST)
        if(brc.profile.codec==BTR_CODEC_LDAC){btr_ldac_media(p,n);return;}
#endif
#if defined(APP_BT_APTX) && !defined(PLAT_HOST)
        if(brc.profile.codec==BTR_CODEC_APTX || brc.profile.codec==BTR_CODEC_APTX_HD){btr_aptx_media(p,n,brc.profile.codec==BTR_CODEC_APTX_HD);return;}
#endif
#ifdef APP_BT_AAC
        if(brc.profile.codec==BTR_CODEC_AAC)btr_aac_media(p,n,brc.profile.conf);
        else
#endif
        btr_media(p,n,brc.profile.conf);
}
static __attribute__((noinline)) int br_frame(const uint8_t *p,uint32_t n)
{
    uint32_t cid=br_le16(p+2),c,len=br_le16(p);uint8_t out[BSD_OUT];
    if(len+4!=n)return 1;
    if(cid==1){brc.signal_at+=br_sig(p+4+brc.signal_at,len-brc.signal_at);return brc.signal_at==len;}
    if(cid<64||cid>=64+BTR_CHANNELS)return 1;c=cid-64;
    if(c && !BTR_SECURE())return 1;
    if(!brc.ch[c].remote || !brc.ch[c].cfg_in || !brc.ch[c].cfg_out)return 1;
    /* Reserve the largest SDP response before invoking its state machine.
     * CPU0 alone enqueues; the IRQ can only free slots during this call. */
    if(c!=2 && !BTR_ROOM((BSD_OUT+4u+16u)/17u))return 0;
    if(c==0){len=bsd_answer(p+4,len,out);if(len)br_send(0,out,len);}
    else if(c==1) {
        uint8_t was=brc.profile.state;
        brc.profile.media=brc.ch[2].cfg_in&&brc.ch[2].cfg_out;
        len=btr_signal(&brc.profile,p+4,len,out);
        /* Initial setup is complete for this ACL once configuration succeeds.
         * CLOSE/ABORT/L2CAP teardown may later return to IDLE before DETACH;
         * that is not a new setup failure. Only br_reset clears this latch. */
        if(brc.profile.state!=BTR_IDLE)brc.profile_ready=1;
#ifdef APP_BT_MULTIPOINT
        if(len && (out[0]&3)==2 &&
           ((out[1]==7 && brc.profile.state==BTR_STREAMING) ||
            (out[1]==3 && br_audio_link==255)))br_stream_claim();
#endif
        if(len){brc.signal=out[1];brc.reply=out[0]&3;brc.error=brc.reply==3?out[len-1]:0;br_send(1,out,len);}
#ifdef APP_BT_AAC_DISPLAY
        if(BTR_OWNS_AUDIO() && len && (out[0]&3)==2 && (out[1]==3 || out[1]==5
#ifdef APP_BT_MULTIPOINT
             || out[1]==7
#endif
             ) && brc.profile.codec==BTR_CODEC_AAC)
            app_bt_aac_prepare();
#ifdef APP_BT_LDAC
        if(BTR_OWNS_AUDIO() && brc.profile.codec==BTR_CODEC_LDAC &&
           (was!=brc.profile.state || (len && out[1]==5 && (out[0]&3)==2)))
            app_bt_ldac_prepare();
#endif
#endif
        if(BTR_OWNS_AUDIO() && (was!=brc.profile.state || (len && out[1]==5 && (out[0]&3)==2))) {
            BTR_FLUSH(btr_config_rate(&brc.profile));
#if defined(APP_BT_LDAC) && !defined(PLAT_HOST)
            /* Reserve decoder storage even in OPEN/CONFIGURED (paused).
             * Otherwise graph availability stays blocked until media arrives.
             * The display callback above detached graphs before this reset;
             * the worker is joined by FLUSH before any storage is reused. */
            if(brc.profile.codec==BTR_CODEC_LDAC && brc.profile.state!=BTR_IDLE)
                btr_ldac_allocate();
#endif
            ba_live=brc.profile.state==BTR_STREAMING;
        }
#ifdef APP_BT_MULTIPOINT
    } else if(c==3) {
        /* This SDP record is a Controller. Unsupported peer commands receive
         * NOT_IMPLEMENTED; never pretend to be an absolute-volume Target. */
        if(len>=6 && !(p[4]&14) && p[5]==0x11 && p[6]==0x0e){
            len=len>64?64:len;memcpy(out,p+4,len);out[0]|=2;out[3]=8;br_send(3,out,len);
        }
#endif
    } else br_media_frame(p+4,len);

    return 1;
}
static int br_control_poll(void)
{
    if(brc.pending && br_frame(brc.rx,brc.need)) {
        brc.pending=0;brc.signal_at=brc.have=brc.need=0;
    }
    return !brc.pending;
}
static int br_append(uint32_t llid,const uint8_t *p,uint32_t n)
{
    if(llid==2)brc.have=brc.need=0;
    else if(llid!=1||!brc.have)return 0;
    if(n>sizeof brc.rx-brc.have){brc.have=brc.need=0;btr_stats.malformed++;return 0;}
    memcpy(brc.rx+brc.have,p,n);brc.have+=n;
    if(brc.have>=4 && !brc.need){brc.need=br_le16(brc.rx)+4;if(brc.need>sizeof brc.rx){brc.have=brc.need=0;return 0;}}
    if(brc.need && brc.have>=brc.need){
        if(brc.have==brc.need)return 1;
        brc.have=brc.need=0;
    }
    return 0;
}
static void br_acl(uint32_t llid,const uint8_t *p,uint32_t n)
{
    if(br_append(llid,p,n)){brc.pending=1;brc.signal_at=0;br_control_poll();}
}
static void br_media_acl(uint32_t llid,const uint8_t *p,uint32_t n)
{
    if(br_append(llid,p,n)){
        if(br_le16(brc.rx+2)==66)br_media_frame(brc.rx+4,brc.need-4);
        brc.have=brc.need=0;
    }
}
