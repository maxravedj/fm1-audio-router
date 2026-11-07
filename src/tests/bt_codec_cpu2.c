/* SPDX-License-Identifier: MPL-2.0 */
/* Both production transports sharing one mailbox; real L2CAP lifecycle dispatch. */
#define TEST_BOTH_CODECS
#define main transport_test_main
#include "bt_aac_transport.c"
#undef main
static void signal_packet(const uint8_t *p,uint32_t n)
{
    uint8_t wire[64]={0};wire[0]=(uint8_t)n;wire[2]=65;memcpy(wire+4,p,n);
    brc.ch[1].remote=71;brc.ch[1].cfg_in=brc.ch[1].cfg_out=1;
    br_acl(2,wire,n+4);
}
static void sbc_packet(const uint8_t *p,uint32_t n,unsigned seq)
{
    uint8_t wire[1100]={0};wire[0]=(uint8_t)(n+13);wire[1]=(uint8_t)((n+13)>>8);wire[2]=66;
    wire[4]=0x80;wire[5]=96;wire[6]=(uint8_t)(seq>>8);wire[7]=(uint8_t)seq;wire[16]=1;memcpy(wire+17,p,n);
    brc.ch[2].remote=72;brc.ch[2].cfg_in=brc.ch[2].cfg_out=1;
    br_acl(2,wire,n+17);
}
int main(int argc,char **argv)
{
    assert(argc==3);memory_limit=100000;hold_worker=1;
    uint8_t sbc[1024];FILE *s=fopen(argv[1],"rb");assert(s);unsigned bytes=fread(sbc,1,sizeof sbc,s);fclose(s);
    unsigned n=btr_frame_size(sbc,bytes,BTR_CAPS);assert(n);
    unsigned expected=4*(1+((sbc[1]>>4)&3))*((sbc[1]&1)?8:4);
    uint8_t conf[6],packet[1200];FILE *a=fopen(argv[2],"rb");assert(a && fread(conf,1,6,a)==6);
    brc.profile.state=BTR_STREAMING;brc.profile.codec=BTR_CODEC_SBC;brc.profile.seid=4;brc.profile.media=1;memcpy(brc.profile.conf,BTR_CAPS,4);
    sbc_packet(sbc,n,1);sbc_packet(sbc,n,2);
    assert(btr_sbc && btr_sbc->active && allocations==1 && !btr_aac && !pcm_frames);
    unsigned started=worker_jobs;
    const uint8_t suspend_sbc[]={0,9,4};signal_packet(suspend_sbc,sizeof suspend_sbc);
    assert(brc.profile.state==BTR_OPEN && !allocations && !worker_fn && !btr_sbc && pcm_frames==expected && worker_jobs==started+1);
    const uint8_t close_sbc[]={0,8,4};signal_packet(close_sbc,sizeof close_sbc);assert(brc.profile.state==BTR_IDLE);
    uint8_t set[]={0,3,8,4,1,0,7,8,0,2,0,0,0,0,0,0};memcpy(set+10,conf,6);signal_packet(set,sizeof set);
    assert(brc.profile.codec==BTR_CODEC_AAC && brc.profile.state==BTR_CONFIGURED && prepared==1);
    const uint8_t open_aac[]={0,6,8};signal_packet(open_aac,sizeof open_aac);
    const uint8_t start_aac[]={0,7,8};signal_packet(start_aac,sizeof start_aac);
    assert(brc.profile.state==BTR_STREAMING && !btr_sbc);
    do {bytes=u32(a);assert(bytes<=sizeof packet && fread(packet,1,bytes,a)==bytes);deliver(packet,bytes,conf);}while(!(packet[1]&128));
    fclose(a);assert(btr_aac && btr_aac->active && !btr_sbc && allocations==2);
    started=worker_jobs;unsigned frames=pcm_frames;
    const uint8_t suspend_aac[]={0,9,8};signal_packet(suspend_aac,sizeof suspend_aac);
    assert(!btr_aac && !worker_fn && !allocations && pcm_frames==frames+1024 && worker_jobs==started+1);
    br_reset();assert(!allocations && !worker_fn);
    brc.profile.state=BTR_STREAMING;brc.profile.codec=BTR_CODEC_SBC;brc.profile.seid=4;brc.profile.media=1;memcpy(brc.profile.conf,BTR_CAPS,4);
    sbc_packet(sbc,n,1);assert(btr_sbc && worker_fn && !btr_aac);
    frames=pcm_frames;br_reset();assert(pcm_frames==frames+expected && !allocations && !worker_fn);
    puts("PASS: actual L2CAP forced SBC/AAC, suspend/reconfigure/disconnect joins shared worker and releases codec scratch");
}
