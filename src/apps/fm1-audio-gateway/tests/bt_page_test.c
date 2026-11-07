/* SPDX-License-Identifier: MPL-2.0 */
/* Actual incoming device FSM with a memory-backed register model. No RF model. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../../bsp/chip/fm1_bredr_frame.h"
#include "../../../bt/br/bt_inq_core.c"
#include "../../../bt/br/bt_audio.h"
#include "../../../bt/br/bt_receiver_page.h"
#define FM1_BR_OFF_FRAME 368u
#define FM1_BR_OFF_FHS 3936u
#define BTI_OFF_FHS_TX 4200u
#define BTI_OFF_EIR_TX 4224u
#define BTI_CLK 0x07ffffffu
#define BTI_M_PAGE 2
#define BTI_M_CONN 3
#define BTI_K_CONN 1
#define BTI_K_LMP 2
#define BTI_K_ACL 3
static uint64_t table[640];
#define bti_tab ((uint8_t *)table)
static struct { uint8_t own[6],mode,o_sweep,train;uint32_t last_rx_clkn; } bti;
static uint32_t fm1_ms, clock_now,alarm_slots,window=46,events,agc;
static unsigned starts,stops,prepare_fail;
static struct { uint32_t kind,idx,len;uint8_t data[340]; } event[16];
static struct fm1_br_frame *fm1_br_frame(uint8_t *t,unsigned n){assert(!n);return (void *)(t+FM1_BR_OFF_FRAME);}
static void fm1_br_xchg(uint8_t *t,unsigned i,unsigned v){assert(i<16);((uint16_t *)t)[i]=v;}
static unsigned name_checks;
static void fm1_br_eir_tx(unsigned n,unsigned t,unsigned p)
{
    if(!n)return;
    const uint8_t *eir=bti_tab+p;
    assert(n==21 && t==10 && p==BTI_OFF_EIR_TX);
    assert(eir[0]==16 && eir[1]==9 && !memcmp(eir+2,"FM1 BT Receiver",15));
    assert(eir[17]==3 && eir[18]==3 && eir[19]==0x0b && eir[20]==0x11);
    name_checks++;
}
static unsigned fm1_br_rx_window(void){return window;}
static void fm1_br_rx_window_set(unsigned v){window=v;}
static void bti_frame_defaults(struct fm1_br_frame *f){memset(f,0,sizeof *f);f->txphdr[0]=f->txphdr[1]=0x6007;}
static void bti_giac(struct fm1_br_frame *f){(void)f;}
static void bti_defaults(void){}
static int bti_prepare(unsigned m){if(prepare_fail)return 0;bti.mode=(uint8_t)m;bti_frame_defaults(fm1_br_frame(bti_tab,0));return 1;}
static void blt_reset(void){}
static void bti_own_addr(void){}
static unsigned bti_clkn(void){return clock_now;}
static void bti_go(unsigned n){(void)n;starts++;bti.last_rx_clkn=UINT32_MAX;}
static void bti_run_stop(void){stops++;}
static void fm1_br_irqs_hold(int on){(void)on;}
static void shim_log(const char *s){(void)s;}
static void bti_alarm_arm(unsigned n){alarm_slots=n;}
static void bti_agc_follow(struct fm1_br_frame *f){(void)f;agc++;}
static void blt_fill(struct fm1_br_frame *f,uint8_t *t){(void)f;(void)t;}
static int bti_is_data(unsigned t){return t==3||t==4||t==10||t==11||t==14||t==15;}
static void bti_push(unsigned k,unsigned idx,unsigned s,unsigned c,const uint8_t *p,unsigned n)
{
    (void)s;(void)c;assert(events<16 && n<=340);event[events].kind=k;event[events].idx=idx;event[events].len=n;
    if(n)memcpy(event[events].data,p,n);events++;
}
#include "../../../bt/br/bt_receiver_page.c"
static void receive(unsigned status,unsigned timestamp,unsigned clk,int buffer)
{
    struct fm1_br_frame *f=fm1_br_frame(bti_tab,0);unsigned idx=(f->rxtog&1)^1;
    f->rxstat[idx]=status;f->rxbit=timestamp;f->rx_clkn_lo=(uint16_t)clk;f->rx_clkn_hi=(uint16_t)(clk>>16);
    f->rxbufcntl[idx]=buffer;bp_rx();
}
static void open_page(unsigned raw,unsigned tx,unsigned local)
{
    struct fm1_br_frame *f=fm1_br_frame(bti_tab,0);
    const uint8_t master[]={0x12,0x34,0x56,0x78,0x90,0xab};
    struct bti_fhs fh={0};memcpy(fh.addr,master,6);fh.lt_addr=3;fh.clk=0x123456;
    memset(&brl,0,sizeof brl);events=0;br_scan_program(0,clock_now=1000);
    assert(f->format==5 && f->txphdr[0]==2 && f->txphdr[1]==2);
    for(unsigned i=0;i<16;i++)assert(((uint16_t *)table)[i]==(FM1_BR_OFF_FRAME|(i&1?4096:0)));
    f->txtog=tx;receive(0x8f00,raw,local-2,1);
    assert(brl.phase==BR_PAGE_RESPONSE && f->format==7 && f->wincntl==440 && alarm_slots==16);
    assert(f->linkcntl[tx]==0x4010 && f->linkcntl[tx^1]==0x4000);
    for(unsigned i=0;i<16;i++)assert(((uint16_t *)table)[i]==FM1_BR_OFF_FRAME);
    bti_fhs_build(&fh,bti_tab+FM1_BR_OFF_FHS);
    receive(0x8f02,raw,local,1);
    unsigned offset=((fh.clk*2)-local+(raw<88))&BTI_CLK;
    assert(brl.phase==BR_FHS_ACK && alarm_slots==2 && f->format==3 && f->miscntl==3);
    assert(f->bdaddr[0]==0x3412 && f->bdaddr[2]==0xab90 && !memcmp(brl.remote,master,6));
    assert((f->clk_off_lo|((uint32_t)f->clk_off_hi<<16))==offset);
    clock_now+=2;bp_alarm(clock_now);
    if(raw<101 && raw>=88)offset=(offset+1)&BTI_CLK;
    unsigned bit=raw<101?raw+524:raw-101;
    assert(brl.phase==BR_CONNECTING && f->wincntl==66 && window==46 && f->bitoff==bit);
    assert((f->clk_off_lo|((uint32_t)f->clk_off_hi<<16))==offset && f->rxptr[1]==1768);
    fm1_ms=100;receive(0x8f01,raw,local+4,0); /* first POLL, no buffer */
    assert(brl.phase==BR_LINKED && events==1 && event[0].kind==BTI_K_CONN && brl.last_good==100);
    fm1_ms=200;receive(0x8f00,raw,local+6,0);assert(events==1 && brl.last_good==200);
    fm1_ms=300;receive(0x8f00,raw,local+6,0);assert(brl.last_good==200); /* stale IRQ */
    receive(0x0201,raw,local+8,0);assert(brl.last_good==200);
}
static void drift(void)
{
    struct fm1_br_frame *f=fm1_br_frame(bti_tab,0);
    for(int direction=-1;direction<=1;direction+=2) {
        open_page(301,0,2000);events=0;
        int phase=200;uint32_t initial_off=brl.offset,clk=2100;
        for(int i=0;i<20000;i++) {
            int absolute=200+direction*(i+1),wrapped=(absolute%625+625)%625;
            uint32_t expected_off=(initial_off-(absolute-wrapped)/625)&BTI_CLK;
            /* Independent radio-window model: a header is received only if
             * the previous phase estimate lies within half the RX window. */
            int error=wrapped-(int)brl.bit;
            if(error>312)error-=625;if(error< -312)error+=625;
            assert(error>=-33 && error<=33);
            fm1_ms+=10;clk+=16;phase=wrapped;
            receive(i%7?0x8f01:0xaf01,(phase+101)%625,clk,0);
            assert(brl.bit==(uint32_t)phase && brl.offset==expected_off && f->bitoff==phase);
            assert(brl.last_good==fm1_ms && brl.syncs==(uint32_t)i+3);
            for(unsigned j=0;j<16;j++)assert(((uint16_t *)table)[j]==(FM1_BR_OFF_FRAME|((j&1)==(expected_off&1)?0:4096)));
            uint32_t syncs=brl.syncs,offset=brl.offset;
            receive(0x0201,0,clk+1,0);assert(brl.syncs==syncs && brl.offset==offset && f->bitoff==phase);
        }
        assert(brl.carries>=31 && brl.bad>=20000);
    }
    puts("PASS: Peripheral follows clock through 400 seconds of synthetic +/-100-ppm drift, confirmation pauses, 64 slot wraps, retransmitted POLLs and rejected-header gaps");
}
static void outgoing(void)
{
    struct fm1_br_frame *f=fm1_br_frame(bti_tab,0);
    memset(&brl,0,sizeof brl);memset(&bta,0,sizeof bta);brl.on=1;brl.phase=BR_PAGE_SCAN;
    br_retry_at=3000;br_retry_index=0;bta.nmem=3;
    bta.mem[1].bonded=bta.mem[2].bonded=1;
    for(unsigned i=0;i<6;i++){bti.own[i]=i+1;bta.mem[1].addr[i]=i+20;bta.mem[2].addr[i]=i+30;}
    unsigned previous=starts;fm1_ms=2999;br_reconnect_poll();assert(starts==previous);
    fm1_ms=3000;br_reconnect_poll();assert(starts==previous+1 && brl.central && !memcmp(brl.remote,bta.mem[1].addr,6));
    assert(f->format==4 && f->wincntl==80 && f->txptr[0]==4252);
    struct bti_fhs fh;bti_fhs_parse(bti_tab+4252,&fh);assert(fh.lt_addr==1 && !memcmp(fh.addr,bti.own,6));
    events=0;receive(0x0200,0,1,1);assert(!br_page_step);
    receive(0x8f00,0,2,1);assert(br_page_step==1 && f->format==6 && alarm_slots==16);
    receive(0x8f00,0,2,1);assert(br_page_step==1); /* stale IRQ */
    receive(0x8f00,0,4,1);assert(br_page_step==2 && f->format==2 && f->bdaddr[0]==0x0201 && alarm_slots==32);
    receive(0x8f00,0,6,1);assert(brl.phase==BR_LINKED && bti.mode==BTI_M_CONN && events==1 && event[0].kind==BTI_K_CONN);
    assert(f->rxptr[0]==1088 && f->rxptr[1]==1768 && f->bufcntl&0x100);
    for(unsigned i=0;i<2;i++)assert(!(f->linkcntl[i]&0x8080));
    uint32_t offset=f->clk_off_lo,bit=f->bitoff;f->rxbit=600;fm1_ms+=1;br_rx_good(0x8f00,8,f);
    assert(f->clk_off_lo==offset && f->bitoff==bit && brl.last_good==fm1_ms); /* Central keeps its own clock */
    brl.phase=BR_PAGE_SCAN;brl.central=0;br_retry_at=fm1_ms;br_reconnect_poll();
    assert(!memcmp(brl.remote,bta.mem[2].addr,6)); /* skip unbonded, round-robin bonds */
    br_page_train=BTI_CLK-100;bti.train=0;bp_alarm(1948);assert(bti.train==1 && f->format==132);
    fm1_ms=br_page_at+5119;previous=stops;br_reconnect_poll();assert(brl.central && stops==previous);
    fm1_ms++;br_reconnect_poll();assert(!brl.central && stops==previous+1 && f->format==5);
    previous=starts;fm1_ms=br_retry_at-1;br_reconnect_poll();assert(starts==previous);
    fm1_ms++;brl.pairing=1;br_reconnect_poll();assert(starts==previous);
    brl.pairing=0;brl.on=0;br_reconnect_poll();assert(starts==previous);
    brl.on=1;brl.phase=BR_CONNECTING;br_reconnect_poll();assert(starts==previous);
    brl.phase=BR_PAGE_SCAN;prepare_fail=1;br_reconnect_poll();assert(!brl.central);prepare_fail=0;
    puts("PASS: outgoing bonded page/FHS/POLL handover, stale/error IRQs, own-clock preservation, A/B clock wrap, 5.12-second timeout, 10-second scan gap, bonded rotation and off/pairing/connecting/prepare-failure guards");
}
int main(void)
{
    for(unsigned raw=0;raw<1024;raw++)for(unsigned tx=0;tx<2;tx++) {
        int expected=raw<88?raw+224:raw<400?raw-88:raw-400;
        assert(br_page_bit(raw)==(expected&1023));
        open_page(raw,tx,tx?BTI_CLK-1:65537u);
    }
    struct fm1_br_frame *f=fm1_br_frame(bti_tab,0);
    /* First LMP packet must be queued AFTER connection initialisation. */
    brl.phase=BR_CONNECTING;events=0;f->rxtog=0;f->rxphdr[1]=(5<<3)|3;
    memcpy(bti_tab+1768,"hello",5);receive(0x8f03,200,2222,1);
    assert(events==2 && event[0].kind==BTI_K_CONN && event[1].kind==BTI_K_LMP);
    assert(event[1].len==5 && !memcmp(event[1].data,"hello",5) && !(f->rxbufcntl[1]&1));
    f->rxphdr[1]=(340<<3)|2;receive(0x8f03,200,2224,1);assert(events==2);
    /* Scan recovery after missing/invalid FHS, including clock wrap. */
    br_scan_program(0,clock_now=BTI_CLK-5);receive(0x8f00,100,20,1);
    bp_alarm((clock_now+16)&BTI_CLK);assert(brl.phase==BR_PAGE_SCAN);
    receive(0x8f00,100,22,1);memset(bti_tab+FM1_BR_OFF_FHS,0,20);
    receive(0x8f02,100,24,1);assert(brl.phase==BR_PAGE_RESPONSE); /* LT_ADDR 0 rejected */
    brl.pairing=1;br_scan_program(1,clock_now=1000);assert(brl.phase==BR_INQUIRY);
    bp_alarm(1800);assert(brl.phase==BR_PAGE_SCAN);bp_alarm(2600);assert(brl.phase==BR_INQUIRY);
    brl.pairing=0;bp_alarm(2601);assert(brl.phase==BR_PAGE_SCAN);
    br_listen();assert(brl.phase==BR_PAGE_SCAN);
    assert(name_checks);puts("PASS: inquiry EIR complete local name FM1 BT Receiver and Audio Sink UUID");
    drift();
    outgoing();
    puts("PASS: actual incoming page FSM: 2048 timing/parity/clock-wrap cases, FHS handover, first POLL without DMA, ordered LMP, stale IRQ, failed-page recovery and discovery cancel");
}
