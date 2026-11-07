/* SPDX-License-Identifier: MPL-2.0 */
/* Incoming baseband, shared verbatim by the device and register-model tests.
 * Hardware timing facts/provenance: docs/bt/BT-RECEIVER-PAGE.md. */
static void br_addr(struct fm1_br_frame *f,const uint8_t a[6])
{
    uint8_t parity[5];bti_parity(a[0]|(uint32_t)a[1]<<8|(uint32_t)a[2]<<16,parity);
    for(unsigned i=0;i<3;i++)f->bdaddr[i]=(uint16_t)(a[2*i]|(uint16_t)a[2*i+1]<<8);
    f->bch_lo=(uint16_t)(parity[0]|parity[1]<<8);f->bch_mid=(uint16_t)(parity[2]|parity[3]<<8);f->bch_hi=parity[4];
}
static void br_exchange(uint32_t parity)
{
    for(uint32_t i=0;i<16;i++)fm1_br_xchg(bti_tab,i,FM1_BR_OFF_FRAME|(parity==2 || (i&1)==parity?0:1u<<12));
}
/* RXBIT is a receive timestamp, not the transmit bit offset. */
static uint16_t br_page_bit(uint16_t timestamp)
{
    uint32_t t=timestamp&1023u;
    return (uint16_t)((t<88u?t+224u:t<400u?t-88u:t+624u)&1023u);
}
static void br_page_reply(struct fm1_br_frame *f)
{
    uint32_t next=f->txtog&1;
    f->linkcntl[next]=0x4010;f->linkcntl[next^1]=0x4000;
    f->txphdr[next]=2;
}
static void br_scan_program(int inquiry,uint32_t now)
{
    struct fm1_br_frame *f=fm1_br_frame(bti_tab,0);
    bti_frame_defaults(f);f->clk_off_lo=f->clk_off_hi=0;f->wincntl=0x8022;
    if(inquiry) {
        struct bti_fhs fh={0};memcpy(fh.addr,bti.own,6);fh.eir=1;fh.sr=1;fh.cod=0x240414;
        bti_fhs_build(&fh,bti_tab+BTI_OFF_FHS_TX);bti_giac(f);
        static const uint8_t eir[]={16,9,'F','M','1',' ','B','T',' ','R','e','c','e','i','v','e','r',3,3,0x0b,0x11};
        memcpy(bti_tab+BTI_OFF_EIR_TX,eir,sizeof eir);fm1_br_eir_tx(sizeof eir,10,BTI_OFF_EIR_TX);
        f->format=11;f->txptr[0]=f->txptr[1]=BTI_OFF_FHS_TX;
        f->txphdr[0]=f->txphdr[1]=(uint16_t)(sizeof eir<<3|3);f->linkcntl[0]=0x4102;f->linkcntl[1]=0x4112;
        brl.phase=BR_INQUIRY;
    } else {
        br_addr(f,bti.own);f->format=5;f->linkcntl[0]=0x4000;f->linkcntl[1]=0x4010;
        f->txphdr[0]=f->txphdr[1]=2;
        f->rxptr[0]=f->rxptr[1]=FM1_BR_OFF_FHS;f->lmprxptr[0]=f->lmprxptr[1]=FM1_BR_OFF_FHS;
        fm1_br_eir_tx(0,0,0);brl.phase=BR_PAGE_SCAN;
    }
    f->rxbufcntl[0]=f->rxbufcntl[1]=0;bti.last_rx_clkn=0xffffffffu;
    brl.scan_at=now;br_exchange(0);bti.mode=BTI_M_PAGE;
}
static void br_listen(void)
{
    brl.central=0;
    bti_defaults();bti.o_sweep=0;
    if(!bti_prepare(BTI_M_PAGE))return;
    blt_reset();bti_own_addr();br_scan_program(brl.pairing,bti_clkn());bti_go(0x03ffffffu);
}
static void br_connection(struct fm1_br_frame *f)
{
    int32_t signed_bit;
    /* Narrow the scan window and preserve slot carry when the timestamp wraps. */
    fm1_br_rx_window_set(46);f->wincntl=66;
    signed_bit=(int32_t)(f->rxbit&1023u)-68-(int32_t)((f->wincntl>>1)&2047u);
    if(signed_bit<0 && brl.fhs_bit>=88u)brl.offset=(brl.offset+1)&BTI_CLK;
    brl.bit=(uint16_t)((signed_bit<0?signed_bit+625:signed_bit)&1023);
    br_addr(f,brl.remote);f->clk_off_lo=(uint16_t)brl.offset;f->clk_off_hi=(uint16_t)(brl.offset>>16);
    f->bitoff=brl.bit;f->miscntl=brl.lt;
    f->format=3;f->bufcntl=(uint16_t)((f->bufcntl&~0x0f20u)|3);
    f->txbufcntl[0]=f->txbufcntl[1]=1;
    for(uint32_t i=0;i<2;i++) {
        f->rxptr[i]=f->lmprxptr[i]=(uint16_t)(1088+i*680);
        f->rxbufcntl[i]=0;f->linkcntl[i]=0x4000;f->txphdr[i]=2;
    }
    brl.phase=BR_CONNECTING;brl.started=brl.last_good=fm1_ms;
    brl.sync_seen=0;brl.good=brl.bad=brl.syncs=brl.carries=0;
    f->rxtog=0;f->rxstat[0]|=0x80;f->rxstat[1]|=0x80;
    br_page_reply(f);
    /* A peripheral must accept either exchange parity until synchronised. */
    bti.mode=BTI_M_CONN;br_exchange(2);
}
#include "bt_receiver_reconnect.c"
static void br_rx_good(uint32_t stat,uint32_t clk,struct fm1_br_frame *f)
{
    /* A Peripheral follows the Central throughout the connection, including
     * the human confirmation pause. Initial FHS synchronisation alone drifts
     * out of the 66-us RX window. Recenter once per received slot; payload
     * retransmissions and NULL/POLL headers also supply valid timing. */
    if(!brl.central && (!brl.sync_seen || brl.sync_clk!=clk)) {
        int32_t signed_bit=(int32_t)(f->rxbit&1023u)-68-(int32_t)((f->wincntl>>1)&2047u);
        uint32_t bit=(uint32_t)(signed_bit<0?signed_bit+625:signed_bit)&1023u;
        if(brl.bit<300u && bit>(uint32_t)brl.bit+324u) {
            brl.offset=(brl.offset+1u)&BTI_CLK;brl.carries++;
        } else if(brl.bit>324u && bit+324u<brl.bit) {
            brl.offset=(brl.offset-1u)&BTI_CLK;brl.carries++;
        }
        brl.bit=(uint16_t)bit;f->bitoff=(uint16_t)bit;
        f->clk_off_lo=(uint16_t)brl.offset;f->clk_off_hi=(uint16_t)(brl.offset>>16);
        br_exchange(brl.offset&1u);
        brl.sync_clk=clk;brl.sync_seen=1;brl.syncs++;brl.good++;
        brl.rxbit=f->rxbit;brl.status=(uint16_t)stat;
    } else if(brl.central)brl.good++;
    brl.last_good=fm1_ms;brl.noise^=stat^clk^f->rxbit;
    if(brl.phase==BR_CONNECTING){brl.phase=BR_LINKED;bti_push(BTI_K_CONN,0,stat,clk,0,0);}
}
static void bp_rx(void)
{
    if(brl.closing)return;
    struct fm1_br_frame *f=fm1_br_frame(bti_tab,0);
    uint32_t i,idx=(f->rxtog&1)^1,stat=f->rxstat[idx],type=stat&15;
    uint32_t clk=f->rx_clkn_lo|(uint32_t)f->rx_clkn_hi<<16;
    if(brl.central && brl.phase<BR_CONNECTING){br_central_page_rx(f,idx,stat,clk);return;}
    if(brl.phase>=BR_CONNECTING) {
        uint32_t any=0;
        brl.rxbit=f->rxbit;brl.status=(uint16_t)stat;
        if(stat>>8!=0x8f && stat>>8!=0xaf)brl.bad++;
        for(uint32_t pass=0;pass<2;pass++) {
            i=idx^pass;if(!(f->rxbufcntl[i]&1))continue;
            any=1;
            stat=f->rxstat[i];type=stat&15;
            if(stat>>8==0x8f || stat>>8==0xaf) {
                br_rx_good(stat,clk,f);
                if(stat>>8==0x8f && bti_is_data(type)) {
                    uint32_t llid=f->rxphdr[i]&3,n=(f->rxphdr[i]>>3)&1023;
                    if(n<=339 && llid)bti_push(llid==3?BTI_K_LMP:BTI_K_ACL,llid,stat,clk,bti_tab+1088+i*680,n);
                }
            }
            f->rxbufcntl[i]&=(uint8_t)~1u;f->linkcntl[i]|=1u<<14;
        }
        /* A NULL/POLL has no payload DMA buffer but establishes/refreshes ACL. */
        if(!any && clk!=bti.last_rx_clkn && (stat>>8==0x8f || stat>>8==0xaf))br_rx_good(stat,clk,f);
        bti.last_rx_clkn=clk;
        blt_fill(f,bti_tab);return;
    }
    if(clk==bti.last_rx_clkn)return;bti.last_rx_clkn=clk;
    if(stat>>8!=0x8f)return;
    bti_agc_follow(f);
    if(brl.phase==BR_PAGE_SCAN && type==0) {
        brl.pages++;
        br_exchange(2);f->wincntl=440;f->bitoff=br_page_bit(f->rxbit);
        f->format=7;br_page_reply(f);
        brl.phase=BR_PAGE_RESPONSE;brl.scan_at=bti_clkn();bti_alarm_arm(16);
    } else if(brl.phase==BR_PAGE_RESPONSE && type==2) {
        struct bti_fhs fh;bti_fhs_parse(bti_tab+FM1_BR_OFF_FHS,&fh);
        if(!fh.lt_addr)return;
        brl.fhs++;
        memcpy(brl.remote,fh.addr,6);
        brl.fhs_bit=f->rxbit&1023u;brl.lt=fh.lt_addr;
        brl.offset=((fh.clk<<1)-clk+(brl.fhs_bit<88u))&BTI_CLK;
        f->wincntl=(uint16_t)fm1_br_rx_window();f->bitoff=br_page_bit(f->rxbit);
        br_addr(f,brl.remote);f->clk_off_lo=(uint16_t)brl.offset;f->clk_off_hi=(uint16_t)(brl.offset>>16);
        f->miscntl=brl.lt;f->format=3;br_page_reply(f);
        brl.phase=BR_FHS_ACK;brl.scan_at=bti_clkn();bti_alarm_arm(2);
    }
    f->rxbufcntl[idx]&=(uint8_t)~1;f->linkcntl[idx]|=1u<<14;
}
static int bp_alarm(uint32_t now)
{
    if(brl.closing){blt_fill(fm1_br_frame(bti_tab,0),bti_tab);bti_alarm_arm(18);return 1;}
    if(brl.central && brl.phase<BR_CONNECTING){br_central_alarm(now);return 1;}
    if(brl.phase==BR_FHS_ACK) {br_connection(fm1_br_frame(bti_tab,0));}
    else if(brl.phase<BR_CONNECTING) {
        if(brl.phase==BR_PAGE_RESPONSE) {
            if(((now-brl.scan_at)&BTI_CLK)>=16)br_scan_program(0,now);
        } else if(brl.pairing && ((now-brl.scan_at)&BTI_CLK)>=800)br_scan_program(brl.phase==BR_PAGE_SCAN,now);
        else if(!brl.pairing && brl.phase==BR_INQUIRY)br_scan_program(0,now);
    } else blt_fill(fm1_br_frame(bti_tab,0),bti_tab);
    bti_alarm_arm(18);return 1;
}
