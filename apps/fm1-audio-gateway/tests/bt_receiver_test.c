/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define APP_BT_RECEIVER
#include "../../../bt/br/bt_receiver_pcm.c"
#include "../../../bt/br/bt_receiver_media.c"
static uint8_t ba_live;
static uint8_t sent[32][512];static uint32_t sent_len[32],nsent;
static int send_packet(const uint8_t *p,uint32_t n) {assert(n<=512 && nsent<32);memcpy(sent[nsent],p,n);sent_len[nsent++]=n;return 1;}
#define BTR_SEND(p,n) send_packet(p,n)
#define BTR_FLUSH(rate) do {btr_audio_reset(rate);btr_media_reset();} while(0)
#define BTR_SECURE() 1
#include "../../../bt/br/bt_receiver_l2cap.c"
static uint32_t command(struct btr_profile *s,uint8_t *out,uint8_t signal,const uint8_t *params,uint32_t n)
{
    uint8_t p[64]={0x90,signal};if(n)memcpy(p+2,params,n);return btr_signal(s,p,n+2,out);
}
static void profiles(void)
{
    struct btr_profile s={0};uint8_t o[64],seid=4,conf[]={4,8,1,0,7,6,0,0,0x21,0x15,2,53};
    assert(command(&s,o,1,0,0)==4 && o[0]==0x92 && o[2]==4 && o[3]==8);
    assert(command(&s,o,2,&seid,1)==12 && o[8]==0x3f);
    assert(command(&s,o,7,&seid,1)==4 && (o[0]&3)==3 && !s.state);
    assert(command(&s,o,3,conf,sizeof conf)==2 && s.state==BTR_CONFIGURED && s.codec==BTR_CODEC_SBC);
    assert(command(&s,o,3,conf,sizeof conf)==4 && (o[0]&3)==3);
    assert(command(&s,o,6,&seid,1)==2 && s.state==BTR_OPEN);
    {
        uint8_t bad[]={4,7,6,0,0,0xff,0x15,2,53},saved[4];
        memcpy(saved,s.conf,sizeof saved);
        assert(command(&s,o,5,bad,sizeof bad)==4 && (o[0]&3)==3);
        assert(s.codec==BTR_CODEC_SBC && !memcmp(saved,s.conf,sizeof saved));
    }
    assert(command(&s,o,7,&seid,1)==4 && s.state==BTR_OPEN);
    s.media=1;assert(command(&s,o,7,&seid,1)==2 && s.state==BTR_STREAMING);
    assert(command(&s,o,9,&seid,1)==2 && s.state==BTR_OPEN);
    assert(command(&s,o,8,&seid,1)==2 && s.state==BTR_IDLE);
    for(unsigned i=0;i<256;i++) {
        conf[8]=(uint8_t)i;s=(struct btr_profile){0};command(&s,o,3,conf,sizeof conf);
        unsigned valid=(i==0x11||i==0x12||i==0x14||i==0x18||i==0x21||i==0x22||i==0x24||i==0x28);
        assert((s.state==BTR_CONFIGURED)==valid);
        assert(s.codec==(valid?BTR_CODEC_SBC:BTR_CODEC_NONE));
    }
    uint32_t rnd=1;uint8_t p[1024];
    for(unsigned trial=0;trial<100000;trial++) {
        unsigned n=trial%sizeof p;
        for(unsigned i=0;i<n;i++){rnd=rnd*1664525+1013904223;p[i]=(uint8_t)(rnd>>24);}
        s=(struct btr_profile){0};btr_signal(&s,p,n,o);
        btr_media(p,n,BTR_CAPS);
    }
    puts("PASS: SBC-only AVDTP states, all rate/mode masks, 100000 malformed signalling/media packets");
}
static void l2cap(void)
{
    uint8_t conn[]={8,0,1,0,2,1,4,0,0x19,0,0x60,0};
    br_reset();nsent=0;br_acl(2,conn,5);br_acl(1,conn+5,sizeof conn-5);
    assert(nsent==2 && brc.ch[1].remote==0x60);
    assert(sent[0][4]==3 && sent[0][8]==65 && sent[0][10]==0x60 && !sent[0][12]);
    assert(sent[1][4]==4);
    /* Incoming and outgoing configuration complete separately. */
    uint8_t req[]={12,0,1,0,4,8,8,0,65,0,0,0,1,2,0xf0,3};
    br_acl(2,req,sizeof req);assert(brc.ch[1].cfg_in && brc.ch[1].mtu==1008);
    uint8_t rsp[]={10,0,1,0,5,0,6,0,65,0,0,0,0,0};rsp[5]=brc.ch[1].id;
    br_acl(2,rsp,sizeof rsp);assert(brc.ch[1].cfg_out);
    uint8_t discover[]={2,0,65,0,0x30,1};br_acl(2,discover,sizeof discover);
    assert(sent[nsent-1][2]==0x60 && sent[nsent-1][4]==0x32 && sent[nsent-1][6]==4);
    nsent=0;
    /* Before OPEN, a second AVDTP channel is refused; unsupported PSM refused. */
    conn[10]=0x61;br_acl(2,conn,sizeof conn);assert(sent[0][12]==2);
    conn[8]=0x17;nsent=0;br_acl(2,conn,sizeof conn);assert(sent[0][12]==2);
    /* SDP searches advertise sink, not source nor unimplemented AVRCP. */
    uint8_t search[]={2,0,1,0,8,0x35,3,0x19,0x11,0x0b,0,8,0},out[BSD_OUT];
    unsigned n=bsd_answer(search,sizeof search,out);assert(n==14 && out[6]==1);
    search[9]=0x0a;n=bsd_answer(search,sizeof search,out);assert(n==10 && !out[6]);
    search[9]=0x0c;n=bsd_answer(search,sizeof search,out);assert(n==10 && !out[6]);
    puts("PASS: incoming fragmented L2CAP/configuration/refusal, sink SDP, AVDTP replies");
}
static unsigned media_packet(uint8_t *out,const uint8_t *frame,unsigned bytes,unsigned sequence,uint8_t flags)
{
    memset(out,0,13);out[0]=0x80;out[1]=96;out[2]=(uint8_t)(sequence>>8);out[3]=(uint8_t)sequence;out[12]=flags;
    memcpy(out+13,frame,bytes);return bytes+13;
}
static void offline(const char *name,const char *reference)
{
    FILE *f=fopen(name,"rb"),*ref=fopen(reference,"rb");assert(f&&ref);
    static uint8_t stream[200000],pkt[1100];static int16_t expected[400000];
    unsigned size=fread(stream,1,sizeof stream,f),nr=fread(expected,2,400000,ref);fclose(f);fclose(ref);
    unsigned at=0,seq=65530,got=0;double err=0,peakerr=0;uint8_t conf[4]={0x3f,0xff,2,250};
    btr_audio_reset(44100);btr_media_reset();memset(&btr_stats,0,sizeof btr_stats);
    while(at<size) {
        unsigned len=btr_frame_size(stream+at,size-at,conf);assert(len);
        unsigned frames=4*(1+((stream[at+1]>>4)&3))*((stream[at+1]&1)?8:4);
        unsigned n=media_packet(pkt,stream+at,len,seq++,1);btr_media(pkt,n,conf);
        assert(!btr_stats.decoder_errors && !btr_stats.malformed);
        assert(got+2*frames<=nr);
        for(unsigned i=0;i<2*frames;i++){double e=btr_decoded[i]-expected[got++];err+=e*e;if(fabs(e)>peakerr)peakerr=fabs(e);}
        at+=len;btr_r=btr_w; /* consume everything for the decoder proof */
    }
    printf("decoded %s: %u frames, reference RMS error %.4f, peak %.0f\n",name,btr_stats.frames,sqrt(err/got),peakerr);
    assert(got==nr && sqrt(err/got)<5 && peakerr<20);
    /* Fragmented single SBC frame, sequence rollover, loss, duplicate, bad CRC. */
    btr_audio_reset(44100);btr_media_reset();memset(&btr_stats,0,sizeof btr_stats);
    unsigned len=btr_frame_size(stream,size,conf),half=len/2;
    unsigned n=media_packet(pkt,stream,half,65535,0xc2);btr_media(pkt,n,conf);assert(!btr_stats.frames);
    n=media_packet(pkt,stream+half,len-half,0,0xa1);btr_media(pkt,n,conf);assert(btr_stats.frames==1);
    btr_media(pkt,n,conf);assert(btr_stats.frames==1);
    n=media_packet(pkt,stream,len,2,1);btr_media(pkt,n,conf);assert(btr_stats.gaps==1 && btr_stats.frames==2);
    pkt[3]=3;pkt[16]^=1;btr_media(pkt,n,conf);assert(btr_stats.decoder_errors==1);
}
static void pcm(int drift_test)
{
    static int16_t in[512];int32_t out[512];
    for(unsigned i=0;i<256;i++){in[2*i]=8000;in[2*i+1]=-4000;}
    btr_audio_reset(44100);memset(&btr_stats,0,sizeof btr_stats);
    memset(out,0,sizeof out);btr_pcm_mix(out,256,4096);assert(!out[0]);
    for(unsigned i=0;i<BTR_TARGET/256;i++)btr_pcm_put(in,256);
    btr_pcm_mix(out,256,2048);assert(abs(out[0]-1024000)<100 && abs(out[1]+512000)<100);
    btr_audio_reset(48000);memset(out,0,sizeof out);btr_pcm_mix(out,256,4096);assert(!out[0]);
    /* Clock drift in both directions, ten simulated minutes each, both rates. */
    for(unsigned rate=44100;drift_test && rate<=48000;rate+=3900) for(int drift=-400;drift<=400;drift+=800) {
        btr_audio_reset(rate);memset(&btr_stats,0,sizeof btr_stats);
        for(unsigned i=0;i<BTR_TARGET/256+1;i++)btr_pcm_put(in,256);
        double source=0;
        for(unsigned block=0;block<103360;block++) {
            memset(out,0,sizeof out);btr_pcm_mix(out,256,4096);
            source+=(double)rate/44100*256*(1+drift/1e6);
            while(source>=128){btr_pcm_put(in,128);source-=128;}
        }
        assert(!btr_stats.underruns && !btr_stats.overruns);
        printf("clock drift: %u Hz, %+d ppm, occupancy %u, no underrun/overrun\n",rate,drift,btr_w-btr_r);
    }
    btr_audio_reset(44100);btr_w=0xfffffe00;btr_r=btr_w;
    for(unsigned i=0;i<BTR_RING/256;i++)btr_pcm_put(in,256);btr_pcm_put(in,256);assert(btr_stats.overruns==1);
    memset(out,0,sizeof out);btr_pcm_mix(out,256,4096);assert(out[0]>2000000);
    puts("PASS: bounded stereo PCM, gain, underrun/overrun, reset, index rollover, filtered clock tracking");
}
static void filter_quality(void)
{
    /* All fractional phases: preserve audible treble and reject frequencies
     * above the 44.1k output Nyquist when receiving 48k PCM. */
    for (unsigned rate=44100;rate<=48000;rate+=3900) {
        const int16_t (*filter)[96]=rate==48000?BTR_FIR_48:BTR_FIR_44;
        double worst=1.0;
        for (unsigned phase=0;phase<64;phase++) for(unsigned frac=0;frac<=1024;frac+=512) {
            double re=0,im=0,dc=0;
            for (unsigned k=0;k<96;k++) {
                int c=filter[phase][k]+((filter[phase+1][k]-filter[phase][k])*(int)frac+512)/1024;
                double angle=2*3.141592653589793*20000*k/rate;
                re+=c*cos(angle)/32768;im+=c*sin(angle)/32768;dc+=c/32768.0;
            }
            double gain=hypot(re,im);if(gain<worst)worst=gain;
            assert(fabs(dc-1)<.003 && gain>.97 && gain<1.03);
            if(rate==48000) {
                re=im=0;
                for(unsigned k=0;k<96;k++) {
                    double c=(filter[phase][k]+((filter[phase+1][k]-filter[phase][k])*(int)frac+512)/1024)/32768.0;
                    double angle=2*3.141592653589793*23000*k/rate;
                    re+=c*cos(angle);im+=c*sin(angle);
                }
                assert(hypot(re,im)<.01);
            }
        }
        printf("filter %u Hz: worst 20-kHz gain %.3f dB; DC/alias checks pass\n",rate,20*log10(worst));
    }
}
int main(int argc,char **argv)
{
    assert(argc==3 || argc==4);
    int offline_only=argc==4 && !strcmp(argv[3],"offline-only");
    if(!offline_only){profiles();l2cap();filter_quality();}
    offline(argv[1],argv[2]);
    if(!offline_only)pcm(argc==3);
    puts("PASS: receiver protocol/decoder/PCM");
}
