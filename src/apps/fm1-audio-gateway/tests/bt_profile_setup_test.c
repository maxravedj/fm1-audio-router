/* SPDX-License-Identifier: MPL-2.0 */
/* Profile setup with real L2CAP/AVDTP, mocked encryption/media boundaries.
 * Deliberately not a captured macOS trace or a radio interoperability test. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../bt/br/bt_receiver.h"
#define APP_BT_RECEIVER
#define APP_BT_AAC
#define APP_BT_AAC_DISPLAY
static struct btr_counters btr_stats;
static uint8_t ba_live,last[1024];
static unsigned last_n,secure=1,flushes,prepared,aac_packets,sbc_packets;
static void send_frame(const uint8_t *p,unsigned n){assert(n<=sizeof last);memcpy(last,p,n);last_n=n;}
static void app_bt_aac_prepare(void){prepared++;}
static void btr_aac_media(const uint8_t *p,unsigned n,const uint8_t *c){(void)p;(void)n;(void)c;aac_packets++;}
void btr_media(const uint8_t *p,unsigned n,const uint8_t *c){(void)p;(void)n;(void)c;sbc_packets++;}
#define BTR_SEND(p,n) send_frame(p,n)
#define BTR_SECURE() secure
#define BTR_FLUSH(rate) ((void)(rate),++flushes)
#include "../../../bt/br/bt_receiver_l2cap.c"
static unsigned connect_channel(unsigned remote)
{
    uint8_t p[16];br_acl(2,p,bl_conn_req(p,9,0x19,remote));
    for(unsigned i=1;i<3;i++)if(brc.ch[i].remote==remote)return 64+i;
    assert(0);return 0;
}
static void configure(unsigned cid,const uint8_t *opts,unsigned n,unsigned flags)
{
    uint8_t p[64];assert(n<=sizeof p-12);
    bl_le16(p+8,cid);bl_le16(p+10,flags);memcpy(p+12,opts,n);
    br_acl(2,p,bl_sig(p,BL_CONF_REQ,10,n+4));
}
static void ready(unsigned cid)
{
    uint8_t p[32];br_acl(2,p,bl_conf_rsp(p,brc.ch[cid-64].id,cid,0));
    assert(brc.ch[cid-64].cfg_out);
}
static void signal(const uint8_t *p,unsigned n)
{
    uint8_t frame[68];assert(n<=64);bl_le16(frame,n);bl_le16(frame+2,65);memcpy(frame+4,p,n);br_acl(2,frame,n+4);
}
static const uint8_t options[]={1,2,0xf0,3,2,2,0xa0,0,4,9,0,0,0,0,0,0,0,0,0};
static void options_and_lifecycle(void)
{
    br_reset();unsigned cid=connect_channel(96);assert(cid==65);ready(cid);
    configure(cid,options,sizeof options,0);
    assert(last_n==14 && !last[12] && brc.ch[1].cfg_in && brc.ch[1].mtu==1008);
    /* The last request's unknown mandatory options must be returned verbatim. */
    uint8_t unknown[]={0x25,2,0xab,0xcd,0x26,0};
    configure(cid,unknown,sizeof unknown,0);
    assert(last_n==20 && last[12]==3 && !memcmp(last+14,unknown,sizeof unknown));
    uint8_t hint[]={0xa5,2,0xab,0xcd};configure(cid,hint,sizeof hint,0);assert(!last[12]);
    uint8_t small[]={1,2,47,0};configure(cid,small,sizeof small,0);
    assert(last_n==18 && last[12]==1 && last[14]==1 && last[16]==48 && brc.ch[1].mtu==1008);
    uint8_t ertm[]={4,9,3,0,0,0,0,0,0,0,0};configure(cid,ertm,sizeof ertm,0);
    assert(last_n==25 && last[12]==1 && last[14]==4 && !last[16]);
    uint8_t zero_flush[]={2,2,0,0};configure(cid,zero_flush,4,0);assert(last[12]==2);
    uint8_t malformed[]={1,3,0};configure(cid,malformed,sizeof malformed,0);assert(last[12]==2);
    configure(cid,options,sizeof options,1);assert(last[12]==2); /* no false acceptance of split config */
    /* MPEG-2 AAC-LC, stereo 44.1k VBR 256k, matching the supplied Mac log. */
    uint8_t set[]={0,3,8,4,1,0,7,8,0,2,0x80,1,4,0x83,0xe8,0};
    signal(set,sizeof set);assert(brc.profile.state==BTR_CONFIGURED && prepared==1 && brc.reply==2);
    uint8_t open[]={0x10,6,8};signal(open,sizeof open);assert(brc.profile.state==BTR_OPEN);
    cid=connect_channel(97);assert(cid==66);ready(cid);configure(cid,options,sizeof options,0);
    uint8_t start[]={0x20,7,8};signal(start,sizeof start);assert(brc.profile.state==BTR_STREAMING && ba_live);
    uint8_t media[]={1,0,66,0,0};br_acl(2,media,sizeof media);assert(aac_packets==1 && !sbc_packets);
    secure=0;br_acl(2,media,sizeof media);assert(aac_packets==1);secure=1;
    uint8_t suspend[]={0x30,9,8};signal(suspend,sizeof suspend);assert(!ba_live && brc.profile.state==BTR_OPEN);
    signal(start,sizeof start);assert(ba_live);
    uint8_t close[]={0x40,8,8};signal(close,sizeof close);assert(!ba_live && brc.profile.state==BTR_IDLE);
    signal(start,sizeof start);assert(brc.signal==7 && brc.reply==3 && brc.error==0x31);
    br_reset();assert(!brc.signal && !brc.last_psm && !brc.conf_cid && !ba_live);
    puts("PASS: explicit Flush Timeout/Basic mode, bounded option negotiation, actual AAC OPEN/media/START lifecycle, encryption gate and setup diagnostics");
}
static void malformed_configurations(void)
{
    uint32_t seed=12;uint8_t p[40];
    br_reset();connect_channel(96);
    for(unsigned trial=0;trial<20000;trial++){
        unsigned n=trial%sizeof p;
        for(unsigned i=0;i<n;i++){seed=seed*1664525u+1013904223u;p[i]=(uint8_t)(seed>>24);}
        brc.ch[1].cfg_in=0;configure(65,p,n,0);
        assert(last_n<=32 && last_n>=14);
        if(!last[12])assert(brc.ch[1].cfg_in && brc.ch[1].mtu>=48);
        else assert(!brc.ch[1].cfg_in);
    }
    puts("PASS: 20000 malformed/random L2CAP option lists, bounded responses and no false channel readiness");
}
int main(void){options_and_lifecycle();malformed_configurations();}
