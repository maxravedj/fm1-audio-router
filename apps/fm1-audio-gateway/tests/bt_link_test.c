/* SPDX-License-Identifier: MPL-2.0 */
/* Entire device receiver session against register/IRQ mocks. Real LMP/SSP/TX,
 * profile/media and decoder code; no radio modulation or physical device. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#define APP_BT_RECEIVER
#include "../../../bsp/chip/fm1_bredr_frame.h"
#include "../../../bt/br/bt_inq_core.c"
#include "../../../bt/br/bt_audio.h"
#define FM1_BR_OFF_FRAME 368u
#define FM1_BR_OFF_FHS 3936u
#define FM1_BR_OFF_FRQ 32u
#define BTI_OFF_FHS_TX 4200u
#define BTI_OFF_EIR_TX 4224u
#define BTI_CLK 0x07ffffffu
#define BTI_M_PAGE 2
#define BTI_M_CONN 3
#define BTI_K_CONN 6
#define BTI_K_LMP 5
#define BTI_K_ACL 7
struct bti_rx {uint8_t kind,idx;uint32_t len;uint8_t data[340];};
static uint64_t table[650];
#define bti_tab ((uint8_t *)table)
static struct { uint8_t own[6],mode,o_sweep,train;uint32_t last_rx_clkn; } bti;
static uint32_t fm1_ms,clock_now,alarm_slots,window=46,events,irq_depth;
static uint8_t ba_live;
static int busy,log_count;
static struct fm1_br_frame *fm1_br_frame(uint8_t *t,unsigned n){assert(!n);return (void *)(t+FM1_BR_OFF_FRAME);}
static void fm1_br_xchg(uint8_t *t,unsigned i,unsigned v){assert(i<16);((uint16_t *)t)[i]=v;}
static void fm1_br_eir_tx(unsigned n,unsigned t,unsigned p){(void)n;(void)t;(void)p;}
static unsigned fm1_br_rx_window(void){return window;}
static void fm1_br_rx_window_set(unsigned v){window=v;}
static void fm1_br_afh_map(unsigned n,const uint32_t *p){(void)n;(void)p;}
static void fm1_br_irqs_hold(int on){(void)on;}
static void bti_frame_defaults(struct fm1_br_frame *f){memset(f,0,sizeof *f);f->txphdr[0]=f->txphdr[1]=0x6007;}
static void bti_giac(struct fm1_br_frame *f){(void)f;}
static void bti_defaults(void){}
static int bti_prepare(unsigned m){assert(!busy);bti.mode=(uint8_t)m;bti_frame_defaults(fm1_br_frame(bti_tab,0));return 1;}
static void bti_own_addr(void){}
static unsigned bti_clkn(void){return clock_now;}
static void bti_go(unsigned n){(void)n;busy=1;}
static void bti_alarm_arm(unsigned n){alarm_slots=n;}
static void bti_agc_follow(struct fm1_br_frame *f){(void)f;}
static int bti_is_data(unsigned t){return t==3||t==4||t==10||t==11||t==14||t==15;}
static int bti_busy(void){return busy;}
static void bti_run_stop(void){busy=0;}
static unsigned irq_save(void){return irq_depth++;}
static void irq_restore(unsigned saved){assert(irq_depth==saved+1);irq_depth=saved;}
static unsigned fm1_ticks(void){return fm1_ms*24000;}
static unsigned fm1_ble_rng32(void){static unsigned r=17;return r=r*1664525+1013904223;}
static void shim_log(const char *s){assert(strlen(s)<80);log_count++;}
static char *put_dec(char *s,uint32_t n){char rev[10];unsigned k=0;do{rev[k++]='0'+n%10;n/=10;}while(n);while(k)*s++=rev[--k];*s=0;return s;}
static struct bta_mem *bta_mem_of(const uint8_t a[6]){return bta.nmem && !memcmp(a,bta.mem[0].addr,6)?&bta.mem[0]:0;}
static void bta_remember(const uint8_t a[6],const char *name){memcpy(bta.mem[0].addr,a,6);(void)name;bta.nmem=1;}
static void bti_push(unsigned k,unsigned idx,unsigned s,unsigned c,const uint8_t *p,unsigned n){(void)k;(void)idx;(void)s;(void)c;(void)p;(void)n;events++;}
#include "../../../bt/br/bt_receiver_link.c"
static void packet_tid(unsigned op,unsigned tid,const uint8_t *params,unsigned n)
{
    struct bti_rx r={0};r.kind=BTI_K_LMP;
    if(op>>8==BTL_ESCAPE){r.data[0]=(BTL_ESCAPE<<1)|tid;r.data[1]=(uint8_t)op;r.len=2;}
    else{r.data[0]=(uint8_t)((op<<1)|tid);r.len=1;}
    if(n)memcpy(r.data+r.len,params,n);r.len+=n;
    bp_print_rx(&r);
}
static void packet(unsigned op,const uint8_t *params,unsigned n){packet_tid(op,0,params,n);}
static void drain(void)
{
    struct fm1_br_frame *f=fm1_br_frame(bti_tab,0);
    for(unsigned i=0;i<32;i++) {
        f->txbufcntl[0]=f->txbufcntl[1]=1;blt_publish();blt_fill(f,bti_tab);
        if(blt.r==blt.w)break;
    }
    assert(blt.r==blt.w);
}
static void start_link_mode(int bond,int central,int pairing)
{
    struct fm1_br_frame *f=fm1_br_frame(bti_tab,0);
    memset(&brl,0,sizeof brl);memset(&brd,0,sizeof brd);memset(&bta,0,sizeof bta);
    brl.on=1;brl.central=central;brl.pairing=pairing;brl.phase=BR_LINKED;brl.started=brl.last_good=fm1_ms=100;
    for(unsigned i=0;i<6;i++)bti.own[i]=i+1,brl.remote[i]=10+i;
    if(bond){bta.nmem=1;memcpy(bta.mem[0].addr,brl.remote,6);bta.mem[0].bonded=1;for(unsigned i=0;i<16;i++)bta.mem[0].lk[i]=20+i;}
    busy=1;bti.mode=BTI_M_CONN;bti_frame_defaults(f);f->wincntl=66;brl.bit=200;
    blt_reset();struct bti_rx r={0};r.kind=BTI_K_CONN;bp_print_rx(&r);drain();
    assert(brl_ssp.peripheral==!central && brl.allowed==pairing && !brl.setup && !brl.encrypted && !brl.setup_sent);
}
static void start_link_role(int bond,int central){start_link_mode(bond,central,!bond);}
static void start_link(int bond){start_link_role(bond,0);}
static void authenticated(void)
{
    uint8_t random[16],sres[4],aco[12];for(unsigned i=0;i<16;i++)random[i]=50+i;
    packet(BTL_HOST_CONN_REQ,0,0);packet(BTL_SETUP_COMPLETE,0,0);drain();assert(brl.setup);
    bts_e1(brl_ssp.lk,random,bti.own,sres,aco);packet(BTL_AU_RAND,random,16);
    assert(brl_ssp.st==BTP_MODEREQ && !memcmp(brl_ssp.aco,aco,12));drain();
    assert(brd.last_tx==BTL_ENCRYPTION_MODE_REQ && brl.phase==BR_LINKED);
}
static void enable_encryption(unsigned tid)
{
    uint8_t param[16],kc[16];
    /* Valid POLLs while the Central has not answered our request yet. */
    struct fm1_br_frame *f=fm1_br_frame(bti_tab,0);
    for(unsigned i=0;i<1200;i++) {
        fm1_ms+=10;br_rx_good(0x8f01,i+1,f);bp_poll();assert(busy && brl_ssp.st==BTP_MODEREQ);
    }
    if(tid) {param[0]=BTL_ENCRYPTION_MODE_REQ;packet_tid(BTL_ACCEPTED,1,param,1);}
    else {param[0]=1;packet(BTL_ENCRYPTION_MODE_REQ,param,1);drain();
        param[0]=BTL_ENCRYPTION_MODE_REQ;param[1]=0x23;packet_tid(BTL_NOT_ACCEPTED,1,param,2);}
    assert(brl_ssp.st==BTP_KEYSIZE && !brl.closing);drain();
    param[0]=16;packet_tid(BTL_ENCRYPTION_KEY_SIZE_REQ,tid,param,1);drain();
    for(unsigned i=0;i<16;i++)param[i]=100+i;
    bts_e3(brl_ssp.lk,param,brl_ssp.aco,kc);
    uint32_t sent=brd.sent;
    packet_tid(BTL_START_ENCRYPTION_REQ,tid,param,16);
    assert(brl.encrypted && !brl.pairing && brd.sent==sent && !memcmp(brl_ssp.kc,kc,16));
    /* Key and both encryption directions are applied BEFORE acceptance can be
     * handed to the engine. Each TX payload is checked in the actual queue. */
    for(unsigned i=0;i<8;i++)assert(f->cryptk[i]==(kc[2*i]|kc[2*i+1]<<8));
    assert((f->linkcntl[0]&0x8080)==0x8080 && (f->linkcntl[1]&0x8080)==0x8080);
    assert(blt.pub<blt.w && blt.q[blt.r%BLT_Q][0]==((BTL_ACCEPTED<<1)|tid) && blt.q[blt.r%BLT_Q][1]==BTL_START_ENCRYPTION_REQ);
    drain();assert(brd.sent==sent+1 && (f->linkcntl[0]&0x8080)==0x8080 && (f->linkcntl[1]&0x8080)==0x8080);
}
/* Android's reconnect may never challenge us: it only answers our challenge. */
static void saved_bond_reconnect(int bad)
{
    uint8_t sres[4],aco[12];
    start_link(1);
    packet(BTL_HOST_CONN_REQ,0,0);packet(BTL_SETUP_COMPLETE,0,0);
    assert(brl_ssp.st==BTP_AUTHING && brl_ssp.auth_pending==1 && brl_ssp.tid==1);
    unsigned at=(blt.w-1)%BLT_Q,w=blt.w;
    assert(blt.q[at][0]==((BTL_AU_RAND<<1)|1) && blt.ql[at]==17);
    bts_e1(brl_ssp.lk,blt.q[at]+1,brl.remote,sres,aco);
    packet(BTL_SETUP_COMPLETE,0,0);assert(blt.w==w);drain();
    struct fm1_br_frame *f=fm1_br_frame(bti_tab,0);
    for(unsigned i=0;i<1200;i++){fm1_ms+=10;br_rx_good(0x8f01,i+1,f);bp_poll();}
    assert(busy && brl_ssp.st==BTP_AUTHING && !brl.encrypted);
    if(bad)sres[0]^=1;
    packet_tid(BTL_SRES,1,sres,4);
    if(bad){assert(brl.closing && !brl.encrypted && br_fault.reason==BR_FAIL_SSP);return;}
    assert(brl_ssp.st==BTP_MODEREQ && !brl_ssp.dev_authed && !memcmp(aco,brl_ssp.aco,12));
    drain();enable_encryption(1);
    /* A clean detach, followed by a long idle poll, must retain the detach
     * snapshot rather than inventing a timeout from the dead link's age. */
    uint8_t reason=0x13;packet(BTL_DETACH,&reason,1);
    assert(br_fault.reason==BR_FAIL_REMOTE && br_fault.detail==0x13);
    unsigned fault_at=br_fault.at;
    fm1_ms+=31000;bp_poll();
    assert(br_fault.reason==BR_FAIL_REMOTE && br_fault.at==fault_at && bti.mode==BTI_M_PAGE);
    assert(!blt.w && !blt.r && !brc.ch[1].remote && !brl.encrypted);
    /* Old RX events can remain queued when DETACH stops the radio. Ignore
     * them after scanning restarts, including otherwise permitted SDP. */
    struct bti_rx stale={0};stale.kind=BTI_K_ACL;stale.idx=2;stale.len=12;
    const uint8_t old_sdp[]={8,0,1,0,2,1,4,0,1,0,0x60,0};
    memcpy(stale.data,old_sdp,sizeof old_sdp);bp_print_rx(&stale);
    packet(BTL_SETUP_COMPLETE,0,0);packet(BTL_DETACH,&reason,1);
    assert(!blt.w && !brc.ch[0].remote && !brl.setup && br_fault.at==fault_at);
    /* Reuse the same bonded device and radio frame, without resetting globals. */
    bti_run_stop();br_listen(); /* the next incoming page cancels our outgoing retry */
    f->linkcntl[0]|=0x8080;f->linkcntl[1]|=0x8080;
    f->rxbit=300;brl.fhs_bit=300;br_connection(f);
    assert(!(f->linkcntl[0]&0x8080) && !(f->linkcntl[1]&0x8080));
    struct bti_rx r={0};r.kind=BTI_K_CONN;bp_print_rx(&r);
    assert(brl_ssp.st==BTP_PAIRED && !brl.setup && !brl.encrypted && !brl.allowed);
    packet(BTL_SETUP_COMPLETE,0,0);assert(brl_ssp.st==BTP_AUTHING && brl_ssp.tid==1);
}
static void acl(const uint8_t *p,unsigned n,unsigned llid)
{
    struct bti_rx r={0};r.kind=BTI_K_ACL;r.idx=(uint8_t)llid;r.len=n;
    memcpy(r.data,p,n);bp_print_rx(&r);
}
static void unencrypted_discovery(void)
{
    uint8_t conn[]={8,0,1,0,2,1,4,0,1,0,0x60,0};
    start_link(1);
    acl(conn,5,2);assert(!brc.ch[0].remote);acl(conn+5,sizeof conn-5,1);
    assert(brc.ch[0].remote==0x60 && !brl.encrypted);
    uint8_t req[]={12,0,1,0,4,8,8,0,64,0,0,0,1,2,0xf0,3};acl(req,sizeof req,2);
    uint8_t rsp[]={10,0,1,0,5,0,6,0,64,0,0,0,0,0};rsp[5]=brc.ch[0].id;acl(rsp,sizeof rsp,2);
    assert(brc.ch[0].cfg_in && brc.ch[0].cfg_out);drain();
    uint8_t search[]={13,0,64,0,2,0,1,0,8,0x35,3,0x19,0x11,0x0b,0,8,0};
    unsigned at=blt.w;acl(search,sizeof search,2);
    assert(blt.w==at+2 && blt.q[at%BLT_Q][2]==0x60 && blt.q[at%BLT_Q][4]==3);
    drain();conn[8]=0x19;conn[10]=0x61;at=blt.w;acl(conn,sizeof conn,2);
    assert(blt.w==at+1 && blt.q[at%BLT_Q][12]==3 && !brc.ch[1].remote);drain();
    uint8_t info[]={6,0,1,0,10,7,2,0,2,0};at=blt.w;acl(info,sizeof info,2);
    assert(blt.w==at+1 && blt.q[at%BLT_Q][4]==11);drain();
    /* Even a stale configured channel cannot bypass the data-plane gate. */
    brc.ch[1].remote=0x61;brc.ch[1].cfg_in=brc.ch[1].cfg_out=1;
    uint8_t discover[]={2,0,65,0,0x30,1};at=blt.w;acl(discover,sizeof discover,2);
    assert(blt.w==at && !ba_live);memset(&brc.ch[1],0,sizeof brc.ch[1]);
    authenticated();enable_encryption(1);at=blt.w;acl(conn,sizeof conn,2);
    assert(brc.ch[1].remote==0x61 && blt.w==at+2 && !blt.q[at%BLT_Q][12]);
}
static void fresh_authentication(int bad)
{
    uint8_t random[16],sres[4],aco[12],reply_aco[12],reply_sres[4];
    start_link(0);
    /* The independent full SSP fixture derives this key via P-192 and the
     * DHKey checks. Seed that completed stage to exercise the real receiver's
     * reverse authentication, TX handover and encryption boundary here. */
    for(unsigned i=0;i<16;i++)brl_ssp.lk[i]=20+i,random[i]=80+i;
    brl_ssp.st=BTP_PAIRED;
    packet(BTL_HOST_CONN_REQ,0,0);packet(BTL_SETUP_COMPLETE,0,0);drain();
    bts_e1(brl_ssp.lk,random,bti.own,reply_sres,reply_aco);
    unsigned at=blt.w;
    packet(BTL_AU_RAND,random,16);
    assert(brl_ssp.st==BTP_AUTHING && !brl.encrypted && !bta.mem[0].bonded);
    assert(blt.w==at+2 && blt.q[at%BLT_Q][0]==BTL_SRES<<1 && !memcmp(blt.q[at%BLT_Q]+1,reply_sres,4));
    assert(blt.q[(at+1)%BLT_Q][0]==BTL_AU_RAND<<1 && blt.ql[(at+1)%BLT_Q]==17);
    bts_e1(brl_ssp.lk,blt.q[(at+1)%BLT_Q]+1,brl.remote,sres,aco);
    /* The simulated phone waits for that challenge; it sends no encryption
     * request after our first SRES. Wait with good headers to mirror Android. */
    drain();assert(brd.last_tx==BTL_AU_RAND);
    struct fm1_br_frame *f=fm1_br_frame(bti_tab,0);
    for(unsigned i=0;i<1200;i++){fm1_ms+=10;br_rx_good(0x8f01,i+1,f);bp_poll();}
    assert(busy && brl_ssp.st==BTP_AUTHING && !brl.encrypted && !bta.mem[0].bonded);
    if(bad)sres[0]^=1;
    packet(BTL_SRES,sres,4);
    if(bad) {
        assert(brl.closing && !brl.encrypted && !bta.mem[0].bonded && br_fault.reason==BR_FAIL_SSP && br_fault.detail==5);
        return;
    }
    assert(brl_ssp.st==BTP_MODEREQ && !brl.encrypted && !brl_ssp.init && !memcmp(brl_ssp.aco,aco,12));
    assert(memcmp(aco,reply_aco,12) && blt.w==at+3);
    assert(blt.q[(at+2)%BLT_Q][0]==((BTL_ENCRYPTION_MODE_REQ<<1)|1) && blt.q[(at+2)%BLT_Q][1]==1);
    enable_encryption(1);
    assert(bta.mem[0].bonded && bta.key_new && !memcmp(bta.mem[0].lk,brl_ssp.lk,16));
}
static void central_reconnect(unsigned reverse)
{
    uint8_t param[16],sres[4],aco[12];start_link_role(1,1);
    unsigned at=blt.w;br_connect_signal();assert(blt.w==at); /* no plaintext profile request */
    param[0]=BTL_HOST_CONN_REQ;packet_tid(BTL_ACCEPTED,0,param,1);assert(brl.setup_sent && !brl.setup);
    packet(BTL_SETUP_COMPLETE,0,0);assert(brl_ssp.st==BTP_AUTHING && brl_ssp.tid==0 && brl_ssp.init);drain();
    bts_e1(brl_ssp.lk,brl_ssp.au,brl.remote,sres,aco);
    for(unsigned i=0;i<16;i++)param[i]=100+i;
    if(reverse==0)packet_tid(BTL_AU_RAND,1,param,16);
    packet(BTL_SRES,sres,4);
    if(reverse){
        assert(brl_ssp.st==BTP_AUTHED && !brl.encrypted);
        if(reverse==1)packet_tid(BTL_AU_RAND,1,param,16);
        else{
            fm1_ms+=1999;brl.last_good=fm1_ms;bp_poll();assert(brl_ssp.st==BTP_AUTHED);
            fm1_ms++;bp_poll();
        }
    }
    assert(brl_ssp.st==BTP_MODEREQ && brl_ssp.tid==0);drain();
    param[0]=BTL_ENCRYPTION_MODE_REQ;packet(BTL_ACCEPTED,param,1);assert(brl_ssp.st==BTP_KEYSIZE);drain();
    param[0]=BTL_ENCRYPTION_KEY_SIZE_REQ;packet(BTL_ACCEPTED,param,1);assert(brl_ssp.st==BTP_STARTING);
    struct fm1_br_frame *f=fm1_br_frame(bti_tab,0);
    for(unsigned i=0;i<8;i++)assert(f->cryptk[i]==(brl_ssp.kc[2*i]|brl_ssp.kc[2*i+1]<<8));
    for(unsigned i=0;i<2;i++)assert((f->linkcntl[i]&0x8080)==0x8000); /* key/RX before acceptance, TX clear */
    drain();param[0]=BTL_START_ENCRYPTION_REQ;packet(BTL_ACCEPTED,param,1);
    assert(brl.encrypted && brl_ssp.st==BTP_ENCRYPTED && brl.setup);
    assert(!brc.profile_ready); /* Every new ACL starts with its setup timeout armed. */
    for(unsigned i=0;i<2;i++)assert((f->linkcntl[i]&0x8080)==0x8080);
    at=blt.w;fm1_ms+=999;brl.last_good=fm1_ms;bp_poll();assert(blt.w==at);
    fm1_ms++;bp_poll();assert(brc.connect_id && blt.w==at+1 && blt.q[at%BLT_Q][4]==BL_CONN_REQ);
    assert(blt.q[at%BLT_Q][8]==0x19 && blt.q[at%BLT_Q][10]==65);drain();
    uint8_t rsp[]={12,0,1,0,3,0,8,0,70,0,65,0,1,0,0,0};rsp[5]=brc.connect_id;
    acl(rsp,sizeof rsp,2);assert(brc.connect_id && !brc.ch[1].remote); /* pending */
    rsp[5]++;rsp[12]=0;acl(rsp,sizeof rsp,2);assert(!brc.ch[1].remote); /* unrelated response */
    uint8_t collision[]={8,0,1,0,2,30,4,0,0x19,0,71,0};at=blt.w;acl(collision,sizeof collision,2);
    assert(!brc.ch[1].remote && blt.q[at%BLT_Q][12]==4);drain();
    rsp[5]=brc.connect_id;acl(rsp,sizeof rsp,2);assert(brc.ch[1].remote==70 && !brc.connect_id);drain();
    uint8_t req[]={12,0,1,0,4,11,8,0,65,0,0,0,1,2,0xf0,3};acl(req,sizeof req,2);
    uint8_t cfg[]={10,0,1,0,5,0,6,0,65,0,0,0,0,0};cfg[5]=brc.ch[1].id;acl(cfg,sizeof cfg,2);
    assert(brc.ch[1].cfg_in && brc.ch[1].cfg_out);drain();
    uint8_t discover[]={2,0,65,0,0x30,1};at=blt.w;acl(discover,sizeof discover,2);
    assert(blt.w==at+1 && blt.q[at%BLT_Q][2]==70 && blt.q[at%BLT_Q][5]==1);drain();
    if(reverse==2){
        fm1_ms=brl.secure_at+15000;brl.last_good=fm1_ms;bp_poll();assert(brl.closing && !brl.encrypted);
        assert(br_fault.reason==BR_FAIL_PROFILE);
        unsigned format=f->format;bp_rx();bp_alarm(clock_now);assert(f->format==format); /* drain DETACH, no fake page */
    }else{
        uint8_t set[]={14,0,65,0,0x40,3,4,12,1,0,7,6,0,0,0x21,0x15,2,53};acl(set,sizeof set,2);
        assert(brc.profile.state==BTR_CONFIGURED && brc.profile_ready);drain();
        if(reverse==3)return; /* Established Central link for teardown ordering tests. */
        fm1_ms=brl.secure_at+16000;brl.last_good=fm1_ms;bp_poll();assert(!brl.closing && brl.encrypted);
        unsigned format=f->format;br_pair(1);
        assert(brl.closing && brl.pairing && f->format==format);
        fm1_ms+=300;bp_poll();assert(!brl.closing && brl.phase==BR_INQUIRY);
        br_pair(0);
    }
    br_drop(0,BR_FAIL_USER,0);fm1_ms=br_retry_at;bp_poll();assert(brl.central);
    br_pair(1);assert(brl.pairing && !brl.central && brl.phase==BR_INQUIRY);
    br_pair(0);assert(!brl.pairing && brl.phase==BR_PAGE_SCAN);
    brl.on=0;fm1_ms+=20000;bp_poll();assert(!brl.central);
}
static void disconnect_ordering(void)
{
    for(unsigned close=0;close<2;close++)for(unsigned late=0;late<2;late++)for(unsigned poll=0;poll<2;poll++){
        central_reconnect(3);
        /* Successful OPEN followed by CLOSE or signalling L2CAP disconnect.
         * The foreground may run between that packet and the LMP detach. */
        uint8_t open[]={3,0,65,0,0x50,6,4};acl(open,sizeof open,2);drain();
        assert(brc.profile.state==BTR_OPEN);
        fm1_ms=brl.secure_at+(late?16000:2000);brl.last_good=fm1_ms;
        uint8_t avclose[]={3,0,65,0,0x60,8,4};
        uint8_t sigclose[]={8,0,1,0,6,42,4,0,65,0,70,0};
        acl(close?sigclose:avclose,close?sizeof sigclose:sizeof avclose,2);drain();
        assert(brc.profile.state==BTR_IDLE);
        if(poll){bp_poll();assert(!brl.closing && brl.encrypted);}
        uint8_t reason=0x13;packet(BTL_DETACH,&reason,1);
        assert(brl.reconnect_hold && !brl.encrypted && brl.phase==BR_PAGE_SCAN);
        for(unsigned i=0;i<12;i++){fm1_ms+=10000;bp_poll();assert(!brl.central && !brl.closing);}
        assert(bta.nmem==1 && bta.mem[0].bonded);
    }
    /* A profile CLOSE can also be codec teardown/reconfiguration. It must
     * neither suppress the later real link-loss retry nor mark manual hold. */
    central_reconnect(3);uint8_t abort[]={3,0,65,0,0x60,10,4};acl(abort,sizeof abort,2);drain();
    fm1_ms=brl.secure_at+16000;brl.last_good=fm1_ms;bp_poll();
    assert(brl.encrypted && !brl.reconnect_hold);
    fm1_ms+=4001;bp_poll();assert(br_fault.reason==BR_FAIL_RX && !brl.reconnect_hold);
    fm1_ms=br_retry_at;bp_poll();assert(brl.central);
    puts("PASS: all 8 AVDTP/L2CAP teardown orderings retain manual-disconnect hold before/after setup deadline; real link loss still reconnects");
}
static void intentional_disconnect(void)
{
    uint8_t reason=0x13;
    start_link(1);authenticated();enable_encryption(0);
    packet(BTL_DETACH,&reason,1);
    assert(brl.reconnect_hold && !brl.encrypted && brl.phase==BR_PAGE_SCAN);
    for(unsigned i=0;i<12;i++){
        fm1_ms+=10000;bp_poll();assert(!brl.central && !brl.closing);
    }
    assert(bta.nmem==1 && bta.mem[0].bonded); /* no need to unpair */
    /* A source can still reconnect while automatic paging is held. */
    brl.phase=BR_LINKED;brl.started=brl.last_good=fm1_ms;busy=1;
    blt_reset();struct bti_rx r={0};r.kind=BTI_K_CONN;bp_print_rx(&r);drain();
    assert(brl.reconnect_hold);authenticated();enable_encryption(0);assert(!brl.reconnect_hold);
    /* An out-of-range timeout still pages the saved source. */
    fm1_ms=brl.last_good+4001;bp_poll();assert(br_fault.reason==BR_FAIL_RX && !brl.reconnect_hold);
    fm1_ms=br_retry_at;bp_poll();assert(brl.central);
    /* A setup-stage detach is not interpreted as an established user disconnect. */
    start_link(1);packet(BTL_DETACH,&reason,1);assert(!brl.reconnect_hold);
    fm1_ms=br_retry_at;bp_poll();assert(brl.central);
    start_link(1);authenticated();enable_encryption(0);packet(BTL_DETACH,&reason,1);
    br_pair(1);assert(!brl.reconnect_hold && brl.pairing);br_pair(0);
    fm1_ms=br_retry_at;bp_poll();assert(brl.central);
    puts("PASS: deliberate encrypted 0x13 disconnect holds outgoing reconnect across scan retries, keeps bond/incoming connect, resumes after manual encryption or pairing, preserves out-of-range and early-setup retry");
}
static void forgotten_peer_key(void)
{
    uint8_t cap[]={3,0,4},reject[]={BTL_AU_RAND,6},saved[16];
    /* Mac forgets us, FM-1 still has its bond, user starts pairing on FM-1. */
    start_link_mode(1,0,1);memcpy(saved,bta.mem[0].lk,16);
    packet(BTL_SETUP_COMPLETE,0,0);drain();
    assert(brl.setup && brl.allowed && brl_ssp.st==BTP_PAIRED && !brl_ssp.auth_pending);
    assert(brd.last_tx!=BTL_AU_RAND); /* wait for Mac's fresh SSP instead of racing its missing key */
    packet(BTL_ESCAPE<<8|BTL_X_IO_CAPABILITY_REQ,cap,sizeof cap);drain();
    assert(brl_ssp.st==BTP_PK_RX && !brl_ssp.bonded && !brl.closing);
    assert(bta.mem[0].bonded && !memcmp(saved,bta.mem[0].lk,16)); /* stored key replaced only after encrypted success */
    /* A known source that still has its key can authenticate during pairing. */
    start_link_mode(1,0,1);authenticated();enable_encryption(0);
    assert(brl.encrypted && !brl.pairing);
    /* Outside explicit pairing, keep the normal saved-key challenge/security. */
    start_link(1);packet(BTL_SETUP_COMPLETE,0,0);drain();
    assert(brl_ssp.st==BTP_AUTHING && brl_ssp.auth_pending);
    packet_tid(BTL_NOT_ACCEPTED,1,reject,sizeof reject);
    assert(brl.closing && !brl.encrypted && brl_ssp.st==BTP_FAILED && br_fault.detail==6);
    assert(brl.reconnect_hold && bta.mem[0].bonded);
    fm1_ms+=303;bp_poll();
    for(unsigned i=0;i<3;i++){fm1_ms+=10000;bp_poll();assert(!brl.central);}
    br_pair(1);assert(!brl.reconnect_hold && brl.pairing);
    /* A stale bond does not grant permission for fresh SSP outside pairing. */
    start_link(1);packet(BTL_ESCAPE<<8|BTL_X_IO_CAPABILITY_REQ,cap,sizeof cap);
    assert(brl.closing && br_fault.reason==BR_FAIL_NOT_PAIRING && !brl.encrypted);
    puts("PASS: forgotten peer key: explicit pairing waits for fresh SSP, retains old bond until success, allows valid saved-key challenge, rejects unauthorized pairing and holds futile missing-key retries");
}
int main(void)
{
    uint8_t param[16];const char *line;
    start_link(1);
    for(unsigned offset=0;offset<=14;offset+=14){
        unsigned q=blt.w%BLT_Q;uint8_t off=(uint8_t)offset;
        packet_tid(BTL_NAME_REQ,1,&off,1);
        assert(blt.ql[q]==17 && blt.q[q][0]==((BTL_NAME_RES<<1)|1));
        assert(blt.q[q][1]==offset && blt.q[q][2]==15);
        for(unsigned j=0;j<14;j++)assert(blt.q[q][3+j]==(offset+j<15?"FM1 BT Receiver"[offset+j]:0));
        drain();
    }
    puts("PASS: actual receiver remote-name response reassembles FM1 BT Receiver with correct length, offsets, TID and zero padding");
    saved_bond_reconnect(0);saved_bond_reconnect(1);unencrypted_discovery();
    puts("PASS: saved-bond reconnect without Central AU_RAND, 12-second POLL pause, one TID1 challenge, SRES validation, encryption, detach snapshot/reconnect reset; unencrypted SDP/control and secure AVDTP gates");
    start_link(1);authenticated();enable_encryption(0);
    puts("PASS: whole receiver session: HOST/SETUP, bond authentication, 12-second pause with POLLs, Peripheral mode request and crossed Central request, actual TX queue/key/flags ordering");
    fresh_authentication(0);fresh_authentication(1);
    puts("PASS: fresh receiver mutual authentication: SRES then reverse challenge, same TID, delayed phone response, latest ACO, active encryption request with TID 1, Central key-size/start, bond only after success; wrong SRES closes without a bond");
    disconnect_ordering();central_reconnect(0);central_reconnect(1);central_reconnect(2);
    puts("PASS: outgoing Central saved-bond authentication with early/late/absent reverse challenge, E0 RX-before-TX ordering, encrypted AVDTP channel initiation/configuration/discovery, pending/collision/stale responses, profile timeout, pairing cancellation and off guard");
    intentional_disconnect();forgotten_peer_key();
    start_link(1);authenticated();fm1_ms+=4001;bp_poll();
    assert(br_fault.valid && br_fault.reason==BR_FAIL_RX && br_fault.sec==BTP_MODEREQ && br_fault.gap==4001 && br_fault.last_tx==BTL_ENCRYPTION_MODE_REQ);
    unsigned count=br_fault.trace_n,at=br_fault.at;
    for(unsigned i=0;i<100;i++){fm1_ms+=1000;brl.pages++;bp_poll();}
    assert(br_fault.at==at && br_fault.trace_n==count && br_fault.sec==BTP_MODEREQ);
    for(unsigned row=0;(line=br_debug_line(row));row++)assert(strlen(line)<=43);
    assert(!strcmp(br_debug_line(3),"BT fail 1 0 0"));
    /* A remote rejection returns false from btp_in, but still sets FAILED. */
    start_link(1);authenticated();param[0]=BTL_SRES;param[1]=5;
    packet(BTL_NOT_ACCEPTED,param,2);assert(brl.closing && br_fault.reason==BR_FAIL_SSP && br_fault.detail==5);
    fm1_ms+=300;bp_poll();assert(!brl.closing && br_fault.reason==BR_FAIL_SSP);
    start_link(1);authenticated();
    brl.last_good=fm1_ms;brl.started=fm1_ms-30001;bp_poll();assert(br_fault.reason==BR_FAIL_AUTH && br_fault.gap==0);
    /* Format worst-case counters without buffer overflow/truncation. */
    memset(&br_fault,255,sizeof br_fault);br_fault.trace_n=16;brd.sent=UINT32_MAX;
    brl.good=brl.bad=brl.syncs=UINT32_MAX;
    for(unsigned row=0;(line=br_debug_line(row));row++)assert(strlen(line)<=43);
    assert(!irq_depth);
    puts("PASS: RX vs auth timeout snapshots, terminal NOT_ACCEPTED handling, 100 retries retain fault/trace, bounded USB debug rows, balanced interrupt locks");
}
