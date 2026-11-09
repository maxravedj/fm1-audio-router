/* SPDX-License-Identifier: MPL-2.0 */
/* Actual media-only RX pump, L2CAP and LCD driver. Decoder/IRQ/SPI timing is
 * modelled: this proves foreground service/ownership, not hardware headroom. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../bt/br/bt_receiver.h"
#define APP_BT_RECEIVER
#define APP_BT_RESAMPLE_CPU2
#define APP_BT_AAC
#define APP_BT_APTX
#define BTI_RING 32u
#define BTI_RING_HAVE 1
#define BTI_K_ACL 7u
#define BR_LINKED 5u
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
struct bti_rx {uint8_t kind,idx;uint32_t len;uint8_t data[340];};
static struct bti_rx ring[BTI_RING];
#define BTI_RX_AT(i) (&ring[(i)%BTI_RING])
static uint32_t bti_rr,bti_rw;
static struct {unsigned on,closing,phase,setup,encrypted;} brl;
static int memory;
static int *btr_sbc,*btr_aptx;
static struct {void *workspace;} aac_mem,*btr_aac;
static unsigned btr_worker_enabled,media_calls[5],dispatches;
static uint8_t ba_live;
static struct btr_counters btr_stats;
static uint32_t us,credit,underruns,drops,expected_seq;
static int simulation,service_enabled=1;
static void media(unsigned codec,const uint8_t *p,unsigned n)
{
    assert(codec>=1 && codec<=4 && n==100);
    assert((p[0]|(unsigned)p[1]<<8)==(expected_seq++&65535));
    for(unsigned i=2;i<n;i++)assert(p[i]==(uint8_t)i);
    media_calls[codec]++;if(simulation)credit+=3000;
}
void btr_media(const uint8_t *p,uint32_t n,const uint8_t *c){(void)c;media(BTR_CODEC_SBC,p,n);}
static void btr_aac_media(const uint8_t *p,uint32_t n,const uint8_t *c){(void)c;media(BTR_CODEC_AAC,p,n);}
static void btr_aptx_media(const uint8_t *p,uint32_t n,int hd){media(hd?BTR_CODEC_APTX_HD:BTR_CODEC_APTX,p,n);}
static void btr_decode_poll_irq(void){dispatches++;}
static uint32_t plat_ms(void){return us/1000;}
static void send_forbidden(const uint8_t *p,unsigned n){(void)p;(void)n;assert(!"UI must never send signalling");}
#define BTR_SEND(p,n) send_forbidden(p,n)
#define BTR_SECURE() brl.encrypted
#define BTR_FLUSH(rate) ((void)(rate),assert(!"UI must never reset/join/free"))
#include "../bt/br/bt_receiver_l2cap.c"
#define plat_bt_receiver_service production_service
#include "../bt/br/bt_receiver_ui.h"
#undef plat_bt_receiver_service
static void plat_bt_receiver_service(void){if(service_enabled)production_service();}
static unsigned packet_seq;
static void push(unsigned kind,unsigned llid,const uint8_t *p,unsigned n)
{
    assert(n<=340);
    if(bti_rw-bti_rr==BTI_RING){drops++;return;}
    struct bti_rx *r=BTI_RX_AT(bti_rw++);r->kind=kind;r->idx=llid;r->len=n;memcpy(r->data,p,n);
}
static void packet(void)
{
    uint8_t p[104]={100,0,66,0};p[4]=(uint8_t)packet_seq;p[5]=(uint8_t)(packet_seq++>>8);
    for(unsigned i=2;i<100;i++)p[i+4]=(uint8_t)i;
    push(BTI_K_ACL,2,p,sizeof p);
}
static void advance(unsigned n)
{
    while(n--){
        us++;
        if(simulation){
            if(credit)credit--;else underruns++;
            if(!(us%3000))packet();
        }
    }
}
/* Simulate the platform's ~44-Mbit/s LCD DMA. Hash completed bytes (not just
 * submitted pointers), so modifying an in-flight canvas would be detected. */
static const uint8_t *dma;
static uint8_t command;
static unsigned dma_bytes;
static uint32_t lcd_hash;
static void fm1_lcd_wait(void)
{
    advance(2+(dma_bytes*8+43)/44);
    for(unsigned i=0;i<dma_bytes;i++)lcd_hash=(lcd_hash^dma[i])*16777619u;
    dma_bytes=0;
}
static void fm1_lcd_send_cmd(uint8_t c){assert(!dma_bytes);command=c;dma=&command;dma_bytes=1;}
static void fm1_lcd_send_data(const void *p,uint32_t n){assert(!dma_bytes);dma=p;dma_bytes=n;}
static void fm1_lcd_deselect(void){}
static void fm1_lcd_hw_init(void){}
static void fm1_lcd_baud(unsigned b){(void)b;}
#include "../device/lcd.c"
static void reset(unsigned codec)
{
    simulation=0;us+=1000;memset(&brc,0,sizeof brc);memset(media_calls,0,sizeof media_calls);
    brl.on=brl.setup=brl.encrypted=1;brl.closing=0;brl.phase=BR_LINKED;btr_worker_enabled=1;
    btr_sbc=btr_aptx=&memory;btr_aac=&aac_mem;aac_mem.workspace=&memory;
    brc.profile.state=BTR_STREAMING;brc.profile.codec=codec;
    brc.ch[2].remote=96;brc.ch[2].cfg_in=brc.ch[2].cfg_out=1;
    bti_rr=bti_rw=UINT32_MAX-3;packet_seq=expected_seq=drops=underruns=0;service_enabled=1;
}
static void ownership(void)
{
    for(unsigned codec=1;codec<=4;codec++){
        reset(codec);packet();
        /* No lazy allocation, pre-encryption media, teardown or idle decode. */
        btr_sbc=btr_aptx=0;btr_aac=0;production_service();assert(bti_rr!=bti_rw);
        btr_sbc=btr_aptx=&memory;btr_aac=&aac_mem;aac_mem.workspace=0;
        if(codec==BTR_CODEC_AAC){production_service();assert(bti_rr!=bti_rw);}aac_mem.workspace=&memory;
        brl.encrypted=0;production_service();assert(bti_rr!=bti_rw);brl.encrypted=1;
        brl.on=0;production_service();assert(bti_rr!=bti_rw);brl.on=1;
        brl.closing=1;production_service();assert(bti_rr!=bti_rw);brl.closing=0;
        btr_worker_enabled=0;production_service();assert(bti_rr!=bti_rw);btr_worker_enabled=1;
        brc.profile.state=BTR_OPEN;production_service();assert(bti_rr!=bti_rw);brc.profile.state=BTR_STREAMING;
        production_service();assert(bti_rr==bti_rw && media_calls[codec]==1);
        for(unsigned i=0;i<12;i++)packet();
        production_service();assert(bti_rw-bti_rr==12); /* once/ms */
        us+=1000;production_service();assert(bti_rw-bti_rr==4); /* eight-fragment bound */
        us+=1000;production_service();assert(bti_rr==bti_rw);
        /* Real reassembly including a header split: an unclassifiable short
         * header is left untouched for the ordinary foreground poll. */
        uint8_t p[104]={100,0,66,0};p[4]=(uint8_t)packet_seq;p[5]=(uint8_t)(packet_seq++>>8);
        for(unsigned i=2;i<100;i++)p[4+i]=(uint8_t)i;
        push(BTI_K_ACL,2,p,19);push(BTI_K_ACL,1,p+19,85);us+=1000;production_service();
        assert(bti_rr==bti_rw && !brc.have);
        push(BTI_K_ACL,2,p,2);us+=1000;uint32_t rr=bti_rr;production_service();assert(bti_rr==rr);
        br_acl(2,p,2);bti_rr++;push(BTI_K_ACL,1,p+2,102);us+=1000;production_service();assert(bti_rr!=bti_rw);
        bti_rr=bti_rw;brc.have=0;
        /* All control classes stay at the head; later media cannot overtake. */
        for(unsigned kind=0;kind<8;kind++)for(unsigned cid=1;cid<=67;cid++){
            if(kind==BTI_K_ACL && cid==66)continue;
            uint8_t control[4]={0,0,(uint8_t)cid,0};push(kind,2,control,4);packet();
            rr=bti_rr;us+=1000;production_service();assert(bti_rr==rr && !brc.have);
            bti_rr=bti_rw; /* ordinary poll boundary simulated */expected_seq=packet_seq;
        }
        assert(!drops);
    }
    puts("PASS: active SBC/AAC/aptX/HD media only, eight-fragment/ms budget, encryption/workspace gates, fragmented L2CAP, FIFO control deferral and ring wrap");
}
static void repaint(void)
{
    static uint16_t canvas[240*16];
    lcd_fill(0,18,240,222,0);
    for(unsigned y=18;y<240;y+=16){
        unsigned h=240-y;if(h>16)h=16;lcd_sync();
        advance(1000); /* graph/text rendering between DMA transfers */
        for(unsigned i=0;i<240*h;i++)canvas[i]=(uint16_t)(i+y);
        lcd_blit(0,y,240,h,canvas);
    }
    lcd_sync();
}
static void screen_changes(void)
{
    for(unsigned codec=1;codec<=4;codec++){
        uint32_t hash[2];
        for(unsigned enabled=0;enabled<=1;enabled++){
            reset(codec);us=(us/3000+1)*3000;credit=30000;simulation=1;
            service_enabled=enabled;lcd_hash=2166136261u;
            for(unsigned i=0;i<20;i++)repaint();
            hash[enabled]=lcd_hash;
            if(!enabled)assert(underruns && drops && !media_calls[codec]);
            else assert(!underruns && !drops && media_calls[codec]>100 && bti_rw-bti_rr<=1);
            simulation=0;
        }
        assert(hash[0]==hash[1]);
    }
    puts("PASS: 20 full page clear/repaints per codec with continuous modelled RX/audio: old path starves, new checkpoints maintain feeding; exact LCD byte stream preserved");
}
int main(void){ownership();screen_changes();}
