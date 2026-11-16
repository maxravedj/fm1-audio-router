/* SPDX-License-Identifier: MPL-2.0
 * Central page sequencing adapted from fm1-base bt_page.c (its MPL-2.0
 * option). No vendor controller.
 * Only saved bonded peers are paged; foreground policy leaves scan intervals. */
static struct {uint8_t step,retry_index;uint32_t retry_at,page_at,train;} br_page[BR_LINKS];
#define br_page_step (br_page[br_current].step)
#define br_retry_index (br_page[br_current].retry_index)
#define br_retry_at (br_page[br_current].retry_at)
#define br_page_at (br_page[br_current].page_at)
#define br_page_train (br_page[br_current].train)
static void br_central_program(void)
{
    struct fm1_br_frame *f=BR_FRAME();
    uint32_t tx=f->txtog&1;
    br_addr(f,brl.remote);f->clk_off_lo=f->clk_off_hi=0;f->miscntl=0;f->wincntl=80;
    f->txptr[0]=f->txptr[1]=4252;
    f->format=(uint8_t)(4|brl.train<<7);
    f->linkcntl[tx]=0x4002;f->linkcntl[tx^1]=0x4001;
    f->txbufcntl[0]&=(uint8_t)~1;f->txbufcntl[1]&=(uint8_t)~1;
    f->txphdr[0]=f->txphdr[1]=2;br_exchange(0);br_page_step=0;
}
static void br_central_page_rx(struct fm1_br_frame *f,uint32_t idx,uint32_t stat,uint32_t clk)
{
    if(clk==brl.last_rx_clkn)return;brl.last_rx_clkn=clk;
    if(stat>>8!=0x8f || (stat&15)!=0)return;
    bti_agc_follow(f);bti.o_sweep=0;
    uint32_t tx=f->txtog&1,pt=br_page_step?1:2,format=br_page_step?2:6;
    if(br_page_step==1){
        br_addr(f,bti.own);br_exchange(0);f->clk_off_lo=f->clk_off_hi=0;
        f->rxtog=0;f->rxstat[0]|=0x90;f->rxstat[1]|=0x90;
    }
    br_page_step++;f->miscntl=(uint8_t)(1u+br_current);f->format=(uint8_t)(brl.train<<7|format);
    f->linkcntl[tx]=(uint16_t)(pt|0x4010);f->linkcntl[tx^1]=(uint16_t)(pt|0x4000);
    f->txbufcntl[tx]&=(uint8_t)~1;bti_alarm_arm(br_page_step==1?16:32);
    if(br_page_step>2){
        f->bufcntl=(uint16_t)((f->bufcntl&~0x0f20u)|0x103);
        for(unsigned i=0;i<2;i++){
            f->txbufcntl[i]|=1;f->rxbufcntl[i]&=(uint8_t)~1;
            f->rxptr[i]=f->lmprxptr[i]=(uint16_t)(BR_RX_OFF+i*680);
            f->linkcntl[i]=(uint16_t)((f->linkcntl[i]&~0x8080u)|0x4000);
        }
        brl.phase=BR_LINKED;brl.started=brl.last_good=fm1_ms;
        brl.good=brl.bad=brl.syncs=brl.carries=0;brl.sync_seen=0;bti.mode=BTI_M_CONN;
        bti_push(BTI_K_CONN,0,stat,clk,0,0);
    }
    f->rxbufcntl[idx]&=(uint8_t)~1;
}
static void br_central_alarm(uint32_t now)
{
    if(((now-br_page_train)&BTI_CLK)>=2048){br_page_train=now;brl.train^=1;}
    br_central_program();bti_alarm_arm(18);
}
static void br_reconnect_poll(void)
{
#ifdef APP_BT_MULTIPOINT
    if(!br_frame_safe())return;
#endif
    if(!brl.on ||
#ifndef APP_BT_MULTIPOINT
       brl.reconnect_hold ||
#endif
       brl.pairing || brl.closing || brl.phase>=BR_PAGE_RESPONSE)return;
    if(brl.central){
        if(fm1_ms-br_page_at<5120)return;
        #ifndef APP_BT_MULTIPOINT
        fm1_br_irqs_hold(1);bti_run_stop();fm1_br_irqs_hold(0);
#endif
        brl.central=0;
        br_retry_at=fm1_ms+10000;br_listen();return;
    }
    if((int32_t)(fm1_ms-br_retry_at)<0 || !bta.nmem)return;
    struct bta_mem *peer=0;
    for(unsigned i=0;i<bta.nmem;i++){
        unsigned index=br_retry_index++%bta.nmem;
        if(bta.mem[index].bonded
#ifdef APP_BT_MULTIPOINT
           && !br_peer_held(bta.mem[index].addr)
#endif
           ){peer=&bta.mem[index];break;}
    }
    br_retry_at=fm1_ms+10000;if(!peer)return;
    #ifndef APP_BT_MULTIPOINT
        fm1_br_irqs_hold(1);bti_run_stop();fm1_br_irqs_hold(0);
#endif
#ifdef APP_BT_MULTIPOINT
    for(unsigned j=0;j<BR_LINKS;j++)if(j!=br_current && br_links[j].phase>=BR_CONNECTING &&
       !memcmp(br_links[j].remote,peer->addr,6))return;
    /* The outgoing page FHS buffer is shared. Only one Central may page at a
     * time; established ACLs have their own DMA buffers and do not use it. */
    for(unsigned j=0;j<BR_LINKS;j++)if(j!=br_current && br_links[j].central &&
       br_links[j].phase<BR_CONNECTING)return;
    fm1_br_irqs_hold(1);
    /* Recheck after masking the scheduler: it could have resumed this frame
     * while foreground selected the bonded peer. No active DMA reset. */
    if(!br_frame_safe()){fm1_br_irqs_hold(0);return;}
    bti_frame_defaults(BR_FRAME());brl.generation++;
#else
    if(!bti_prepare(BTI_M_PAGE)){br_listen();return;}
#endif
    memcpy(brl.remote,peer->addr,6);brl.central=1;brl.phase=BR_PAGE_SCAN;
    brl.setup_sent=0;br_page_at=fm1_ms;br_page_train=bti_clkn();brl.train=0;
    bti_own_addr();struct bti_fhs fh={0};memcpy(fh.addr,bti.own,6);fh.sr=1;fh.cod=0x240414;fh.lt_addr=(uint8_t)(1u+br_current);
    bti_fhs_build(&fh,bti_tab+4252);blt_reset();
    struct fm1_br_frame *f=BR_FRAME();
    for(unsigned i=0;i<2;i++)f->rxptr[i]=f->lmprxptr[i]=FM1_BR_OFF_FHS;
    br_central_program();
#ifndef APP_BT_MULTIPOINT
    bti_go(8192);
#else
    fm1_br_irqs_hold(0);
#endif
    shim_log("BT reconnect: paging saved source");
}
