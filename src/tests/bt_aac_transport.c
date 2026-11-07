/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define APP_BT_AAC
#define APP_BT_AAC_DISPLAY
static void app_bt_aac_prepare(void);
static unsigned prepared;
#define PLAT_TICKS_PER_US 24u
static uint32_t plat_ticks24(void){static uint32_t t;return ++t;}
static __thread unsigned test_core;
static unsigned hold_worker,worker_jobs;
static void (*worker_fn)(uint32_t);
static uint32_t worker_arg;
static int plat_cpu2_run(void (*fn)(uint32_t),uint32_t arg)
{assert(!worker_fn && !test_core);worker_fn=fn;worker_arg=arg;return 1;}
static void plat_cpu2_sync(void){__asm__ volatile("" ::: "memory");}
static int plat_cpu2_busy(void){return worker_fn!=0;}
static void complete_worker(void)
{if(worker_fn){void (*fn)(uint32_t)=worker_fn;worker_fn=0;test_core=1;worker_jobs++;fn(worker_arg);test_core=0;}}
static int plat_cpu2_wait(void){complete_worker();return 0;}
static unsigned allocations;
static int fail_alloc;
static uint32_t memory_limit=32000,memory_used;
static struct {void *p;uint32_t n;} blocks[8];
static void *plat_work_alloc(uint32_t n){
    assert(!test_core);
    if(fail_alloc || n>memory_limit-memory_used)return 0;
    for(unsigned i=0;i<8;i++)if(!blocks[i].p){void *p=calloc(1,n);assert(p);blocks[i].p=p;blocks[i].n=n;memory_used+=n;allocations++;return p;}
    return 0;
}
static void plat_work_free(void *p){assert(!test_core);if(p){for(unsigned i=0;i<8;i++)if(blocks[i].p==p){memory_used-=blocks[i].n;blocks[i].p=0;allocations--;free(p);return;}assert(0);}}
#include "../bt/br/bt_receiver.h"
static struct btr_counters btr_stats;
static FILE *output;
static unsigned pcm_frames;
static void btr_pcm_put(const int16_t *p,uint32_t n){assert(test_core);pcm_frames+=n;if(output)assert(fwrite(p,4,n,output)==n);}
#ifdef TEST_BOTH_CODECS
#define APP_BT_SBC_CPU2
#include "../bt/br/bt_receiver_media.c"
#else
#include "../bt/br/bt_receiver_aac.c"
#endif
static uint8_t ba_live;
static uint32_t selected_rate;
static int secure=1;
#define BTR_SECURE() secure
#define BTR_SEND(p,n) ((void)(p),(void)(n))
#ifdef TEST_BOTH_CODECS
#define BTR_FLUSH(rate) do {selected_rate=(rate);btr_media_reset();} while(0)
#else
#define BTR_FLUSH(rate) do {selected_rate=(rate);btr_aac_reset();} while(0)
#endif
static unsigned sbc_calls;
#ifndef TEST_BOTH_CODECS
void btr_media(const uint8_t *p,uint32_t n,const uint8_t c[4]){(void)p;(void)n;(void)c;sbc_calls++;}
#endif
#include "../bt/br/bt_receiver_l2cap.c"
static void app_bt_aac_prepare(void){prepared++;assert(brc.profile.codec==BTR_CODEC_AAC);}
static void deliver(const uint8_t *p,uint32_t n,const uint8_t conf[6])
{
    uint8_t frame[BTR_MTU+4];assert(n<=BTR_MTU);
    brc.profile.state=BTR_STREAMING;brc.profile.codec=BTR_CODEC_AAC;memcpy(brc.profile.conf,conf,6);
    brc.ch[2].remote=71;brc.ch[2].cfg_in=brc.ch[2].cfg_out=1;
    frame[0]=(uint8_t)n;frame[1]=(uint8_t)(n>>8);frame[2]=66;frame[3]=0;memcpy(frame+4,p,n);
    /* Actual L2CAP Basic-mode ACL start/continuation and codec dispatch. */
    if(!hold_worker)complete_worker();btr_aac_poll();
    br_acl(2,frame,7);br_acl(1,frame+7,n-3);
    if(!hold_worker)while(worker_fn){complete_worker();btr_aac_poll();}
}

static uint32_t u32(FILE *f){uint8_t p[4];assert(fread(p,1,4,f)==4);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void profiles(void)
{
    uint8_t out[64],get[]={0,1},caps[]={0,2,8};struct btr_profile s={0};
    assert(btr_signal(&s,get,2,out)==6 && out[4]==8);
    assert(btr_signal(&s,caps,3,out)==14 && out[7]==2 && out[8]==0xc0 && out[10]==0x8c);
    uint8_t set[]={0,3,8,4,1,0,7,8,0,2,0x40,1,4,0x80,0,0};
    assert(btr_signal(&s,set,sizeof set,out)==2 && s.codec==BTR_CODEC_AAC && s.seid==8);
    uint8_t frame[sizeof set+4]={sizeof set,0,65,0};memcpy(frame+4,set,sizeof set);
    brc.ch[1].remote=77;brc.ch[1].mtu=1008;brc.ch[1].cfg_in=brc.ch[1].cfg_out=1;
    secure=0;br_acl(2,frame,sizeof frame);assert(!prepared);
    secure=1;br_acl(2,frame,sizeof frame);assert(prepared==1);
    br_acl(2,frame,sizeof frame);assert(prepared==1); /* rejected duplicate never reclaims */
    br_reset();
    struct btr_profile saved=s;
    for(unsigned byte=10;byte<=12;byte++)for(unsigned v=0;v<256;v++) {
        uint8_t copy[sizeof set];memcpy(copy,set,sizeof set);copy[byte]=(uint8_t)v;s=(struct btr_profile){0};
        btr_signal(&s,copy,sizeof copy,out);
        if(s.state)assert(btr_aac_conf_valid(s.conf));
    }
    s=saved;uint8_t wrong[]={0,6,4};assert((btr_signal(&s,wrong,3,out)>2) && s.state==BTR_CONFIGURED);
    uint8_t open[]={0,6,8};assert(btr_signal(&s,open,3,out)==2);s.media=1;
    uint8_t start[]={0,7,8};assert(btr_signal(&s,start,3,out)==2 && s.state==BTR_STREAMING);
    uint8_t close[]={0,8,8};assert(btr_signal(&s,close,3,out)==2 && s.state==BTR_IDLE);
    uint8_t sbc_caps[]={0,2,4};assert(btr_signal(&s,sbc_caps,3,out)==12 && out[7]==0 && out[8]==0x3f);
    uint8_t sbc_set[]={0,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53};
    assert(btr_signal(&s,sbc_set,sizeof sbc_set,out)==2 && s.codec==BTR_CODEC_SBC && s.seid==4);
    /* Reconfiguration cannot change a different endpoint or publish invalid AAC. */
    s=saved;s.state=BTR_OPEN;saved=s;
    uint8_t bad_reconf[]={0,5,8,7,8,0,2,0x40,1,0x84,0x80,0,0};
    assert(btr_signal(&s,bad_reconf,sizeof bad_reconf,out)>2 && !memcmp(&s,&saved,sizeof s));
}
static void unmarked(FILE *f,const uint8_t conf[6])
{
    uint8_t packet[1200];uint32_t n,timestamp=0;unsigned elements=0;
    memory_limit=100000;
    while((n=u32(f))) {
        assert(n<=sizeof packet && fread(packet,1,n,f)==n);
        elements+=(packet[1]>>7);timestamp=btr_be32(packet+4);
        packet[1]&=127;deliver(packet,n,conf);
        /* The last element waits for the next timestamp. Fragmentation must
         * not cause early publication or change decoded PCM. */
        assert(pcm_frames<=elements*1024u);
    }
    assert(elements && pcm_frames==(elements-1)*1024u);
    uint16_t seq=(uint16_t)(((uint16_t)packet[2]<<8|packet[3])+1);
    timestamp+=1024;packet[2]=(uint8_t)(seq>>8);packet[3]=(uint8_t)seq;
    for(unsigned j=0;j<4;j++)packet[4+j]=(uint8_t)(timestamp>>(24-8*j));
    packet[12]=0;deliver(packet,13,conf); /* next, incomplete element */
    assert(pcm_frames==elements*1024u && !btr_stats.malformed &&
           !btr_stats.decoder_errors && !btr_stats.gaps);
    /* A timestamp change must reject that incomplete element without PCM,
     * rather than treating the timestamp alone as proof of a valid frame. */
    packet[3]++;packet[6]^=4;deliver(packet,13,conf);
    assert(pcm_frames==elements*1024u && btr_stats.gaps==1);
    btr_aac_reset();assert(!allocations);
    puts("PASS: unmarked complete/fragmented AAC, timestamp boundaries, truncated rejection and scratch release");
}
static void unmarked_recovery(FILE *f,uint8_t conf[6])
{
    uint8_t packet[1200];uint32_t n;
    unsigned before=pcm_frames;memory_limit=32000;
    rewind(f);assert(fread(conf,1,6,f)==6);
    while((n=u32(f))){
        assert(n<=sizeof packet && fread(packet,1,n,f)==n);
        packet[1]&=127;deliver(packet,n,conf);
        if(btr_aac->pending)memory_limit=100000;
    }
    assert(memory_limit==100000 && pcm_frames>before+1024u);
    btr_aac_reset();assert(!allocations);
    /* A sequence gap with markerless data must recover at later complete
     * timestamps, without decoding the element whose fragment was lost. */
    rewind(f);assert(fread(conf,1,6,f)==6);before=pcm_frames;
    unsigned dropped=0;
    while((n=u32(f))){
        assert(n<=sizeof packet && fread(packet,1,n,f)==n);
        if(!dropped && btr_be32(packet+4)==3072){dropped=1;continue;}
        packet[1]&=127;deliver(packet,n,conf);
    }
    assert(dropped && pcm_frames>before+1024u && btr_stats.gaps);
    btr_aac_reset();assert(!allocations);
    puts("PASS: markerless AAC recovers after deferred workspace allocation and packet loss");
}
int main(int argc,char **argv)
{
    assert(argc==3 || argc==4);profiles();FILE *f=fopen(argv[1],"rb");assert(f);uint8_t conf[6];assert(fread(conf,1,6,f)==6);
    uint32_t n, mux_n=0, last_mux=0;uint8_t packet[1200];
    output=fopen(argv[2],"wb");assert(output);
    if(argc==4){assert(!strcmp(argv[3],"--unmarked"));unmarked(f,conf);fclose(output);output=0;unmarked_recovery(f,conf);fclose(f);return 0;}
    while((n=u32(f))) {assert(n<=sizeof packet && fread(packet,1,n,f)==n);mux_n+=n-12;if(packet[1]&128){last_mux=mux_n;mux_n=0;}deliver(packet,n,conf);
        if(btr_aac && btr_aac->pending){assert(!pcm_frames && !btr_aac->decoder);memory_limit=100000;}
    }
    assert(memory_limit==100000);
    assert(pcm_frames && !btr_stats.decoder_errors && !btr_stats.malformed && !sbc_calls);
    fclose(output);output=0;
    secure=0;unsigned blocked_frames=pcm_frames;deliver(packet,12,conf);assert(pcm_frames==blocked_frames);secure=1;
    /* Each proper prefix must fail before any PCM publication; cached config
     * survives malformed media so the next complete element can recover. */
    unsigned frames_before=pcm_frames;
    const uint8_t *last_data=btr_aac->job_mux;
    for(unsigned j=0;j<last_mux;j++)assert(!btr_aac_element(conf,last_data,j));
    assert(pcm_frames==frames_before);
    test_core=1;assert(btr_aac_element(conf,last_data,last_mux));test_core=0;
    uint8_t full[BTR_AAC_MUX+12]={0x80,0xe0,0,10,0,0,0,1,0,0,0,17};
    memcpy(full+12,last_data,last_mux);
    btr_aac->sequence_valid=0;btr_aac->assembling=0;
    frames_before=pcm_frames;btr_aac_media(full,last_mux+12,conf);complete_worker();btr_aac_poll();
    assert(pcm_frames==frames_before+1024);
    btr_aac_media(full,last_mux+12,conf);assert(pcm_frames==frames_before+1024); /* duplicate */
    /* Lose the middle of a fragmented element, then resume at a new timestamp. */
    uint32_t third=last_mux/3;assert(third);
    full[1]=96;full[3]=11;full[7]=2;btr_aac_media(full,third+12,conf);
    full[1]=0xe0;full[3]=13;memcpy(full+12,last_data+third*2,last_mux-third*2);
    btr_aac_media(full,last_mux-third*2+12,conf);assert(pcm_frames==frames_before+1024);
    /* Fresh allocation and replay of the complete stream must work. */
    btr_aac_reset();assert(!allocations);
    rewind(f);assert(fread(conf,1,6,f)==6);
    while((n=u32(f))){assert(n<=sizeof packet && fread(packet,1,n,f)==n);btr_aac_media(packet,n,conf);complete_worker();btr_aac_poll();}
    assert(pcm_frames>frames_before+1024);
    btr_aac_reset();assert(!allocations);
    fail_alloc=1;btr_aac_media(packet,n,conf);assert(!btr_aac && !allocations);fail_alloc=0;
    unsigned seed=17;
    for(unsigned i=0;i<10000;i++){
        seed=seed*1664525u+1013904223u;n=seed%sizeof packet;
        for(unsigned j=0;j<n;j++){seed=seed*1664525u+1013904223u;packet[j]=(uint8_t)(seed>>24);}
        btr_aac_media(packet,n,conf);if(i%31==0)btr_aac_reset();
    }
    btr_aac_reset();assert(!allocations);
    fclose(f);
    printf("PASS: AAC profile, real LATM/RTP decode, bounded malformed packets and lifecycle; %u PCM frames\n",pcm_frames);
    return 0;
}
