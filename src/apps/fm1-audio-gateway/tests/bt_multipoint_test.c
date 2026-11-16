/* SPDX-License-Identifier: MPL-2.0 */
/* Actual receiver/control/SSP against the same register mocks as the link
 * suite. These checks establish ownership/isolation, not RF interoperability. */
#define APP_BT_MULTIPOINT
static unsigned radio_buffered=1;
#define BR_RADIO_BUFFERED() (radio_buffered)
#define main legacy_link_test_main
#include "bt_link_test.c"
#undef main
static void select_peer(unsigned i)
{
    br_current=(uint8_t)i;brl.on=brl.encrypted=brl.setup=1;brl.phase=BR_LINKED;
    brl.started=brl.last_good=fm1_ms;brl.offset=100u+11u*i;brl.bit=200u+30u*i;
    brl_ssp.st=BTP_ENCRYPTED;brl.generation=(uint8_t)(10+i);
    for(unsigned k=0;k<6;k++)brl.remote[k]=(uint8_t)(40+10*i+k);
    struct fm1_br_frame *f=BR_FRAME();bti_frame_defaults(f);
    f->wincntl=66;f->rxbufcntl[0]=f->rxbufcntl[1]=0;
    for(unsigned k=0;k<8;k++)f->cryptk[k]=(uint16_t)(123+i*100+k);
    brc.profile=(struct btr_profile){.state=BTR_OPEN,.codec=BTR_CODEC_SBC,.seid=4,.conf={0x21,0x15,2,53}};
    brc.profile_ready=1;
    for(unsigned ch=1;ch<4;ch++)brc.ch[ch]=(struct btr_channel){.remote=(uint16_t)(96+ch+10*i),.mtu=1008,.cfg_in=1,.cfg_out=1};
    blt_reset();
}
static void signal_peer(unsigned link,unsigned signal,unsigned generation)
{
    struct bti_rx r={.kind=BTI_K_ACL,.idx=2,.link=(uint8_t)link,.generation=(uint8_t)generation,.len=7};
    r.data[0]=3;r.data[2]=65;r.data[4]=0x40;r.data[5]=(uint8_t)signal;r.data[6]=4;
    assert(br_rx_ready(&r));bp_print_rx(&r);
}
static void media_peer(unsigned link)
{
    struct bti_rx r={.kind=BTI_K_ACL,.idx=2,.link=(uint8_t)link,.generation=br_links[link].generation,.len=5};
    r.data[0]=1;r.data[2]=66;
    assert(br_rx_ready(&r));bp_print_rx(&r);
}
static void arbitration(void)
{
    memset(br_links,0,sizeof br_links);memset(br_channels,0,sizeof br_channels);
    br_on=1;br_radio=0;br_switching=0;br_audio_link=255;busy=1;bti.mode=BTI_M_CONN;fm1_ms=100;
    select_peer(0);select_peer(1);
    brc.ch[3].remote=0;brc.avrcp_sent=1;
    signal_peer(0,7,10);assert(br_audio_link==0 && ba_live && br_channels[0].profile.state==BTR_STREAMING);
    signal_peer(1,7,11);assert(br_audio_link==1 && ba_live && br_channels[0].pause==1);
    assert(!br_channels[1].avrcp_sent); /* Accepted START may reopen AVRCP. */
    /* Only START transfers ownership; a rejected START or late old packets do
     * not steal playback back, and standby SUSPEND does not flush active PCM. */
    uint32_t malformed=btr_stats.malformed;
    media_peer(0);assert(btr_stats.malformed==malformed);
    media_peer(1);assert(btr_stats.malformed==malformed+1);
    signal_peer(0,7,10);assert(br_audio_link==1 && br_channels[0].reply==3);
    br_current=0;uint32_t at=blt.w;br_avrcp_poll();br_avrcp_poll();
    assert(!brc.pause && blt.w==at+2);
    assert(blt.q[at%BLT_Q][9]==0x7c && blt.q[at%BLT_Q][10]==0x46);
    assert(blt.q[(at+1)%BLT_Q][10]==0xc6 && blt.q[at%BLT_Q][4]!=blt.q[(at+1)%BLT_Q][4]);
    btr_phase=123;signal_peer(0,9,10);assert(br_audio_link==1 && ba_live && btr_phase==123);
    signal_peer(0,7,10);assert(br_audio_link==0 && br_channels[1].pause==1);
    signal_peer(1,9,10);assert(br_channels[1].profile.state==BTR_STREAMING); /* stale generation */
    br_current=1;btr_phase=456;br_drop(0,BR_FAIL_REMOTE,0x13);
    assert(br_audio_link==0 && ba_live && btr_phase==456 && br_links[1].reconnect_hold && !br_links[0].reconnect_hold);
    assert(br_links[0].encrypted && br_links[0].setup);
    puts("PASS: last accepted START wins, late media/rejected START ignored, labelled AVRCP pause press/release, independent suspend/detach/manual hold and stale-event rejection");
}
static void backpressure(void)
{
    br_current=0;select_peer(0);memset(&brc.ch[1],0,sizeof brc.ch[1]);
    uint8_t pdu[17]={0};for(unsigned i=0;i<BLT_Q;i++)assert(blt_put(pdu,17,3));
    uint8_t old_id=brc.id;br_connect_signal();
    assert(!brc.connect_sent && !brc.connect_id && brc.id==old_id);
    drain();br_connect_signal();assert(brc.connect_sent && brc.connect_id && blt.w-blt.r==1);
    uint32_t queued=blt.w;br_connect_signal();assert(blt.w==queued);drain();
    /* A single compound SDU needs four times the entire TX capacity. Retry
     * its suffix in place and verify each Echo reply exactly once/in order. */
    uint8_t frame[516]={0};bl_le16(frame,512);bl_le16(frame+2,1);
    for(unsigned i=0;i<128;i++){frame[4+4*i]=8;frame[5+4*i]=(uint8_t)(i+1);}
    br_acl(2,frame,339);assert(!brc.pending);br_acl(1,frame+339,sizeof frame-339);
    assert(brc.pending && brc.signal_at && brc.signal_at<512);
    uint32_t saved_at=brc.signal_at;assert(!br_control_poll() && brc.signal_at==saved_at);
    unsigned expected=1;
    do{
        for(uint32_t i=blt.r;i!=blt.w;i++){
            assert(blt.qllid[i%BLT_Q]==2 && blt.ql[i%BLT_Q]==8);
            assert(blt.q[i%BLT_Q][4]==9 && blt.q[i%BLT_Q][5]==expected++);
        }
        drain();br_control_poll();
    }while(brc.pending || blt.r!=blt.w);
    assert(expected==129 && !brc.have && !brc.need);
    /* SSP cannot advance on a full TX queue; another peer's room is not a
     * substitute for this peer's transcript capacity. */
    for(unsigned i=0;i<BLT_Q;i++)assert(blt_put(pdu,17,3));
    struct bti_rx r={.kind=BTI_K_LMP,.link=0,.generation=brl.generation};
    unsigned security=brl_ssp.st;assert(!br_rx_ready(&r) && brl_ssp.st==security);
    drain();assert(br_rx_ready(&r));
    brc.pause=1;for(unsigned i=0;i<BLT_Q-1;i++)assert(blt_put(pdu,17,3));
    queued=blt.w;br_avrcp_poll();assert(brc.pause==1 && blt.w==queued);drain();
    puts("PASS: full control queue retains outgoing connect/SSP/AVRCP state; 128 compound replies retry without loss, duplication or reordering");
}
static void discovery(void)
{
    uint8_t request[]={6,0,5,0,15,0x35,3,0x19,0x10,0x02,4,0,0x35,5,0x0a,0,0,255,255,0};
    struct {uint8_t data[BSD_OUT];uint8_t guard[16];} out;memset(&out,0xa5,sizeof out);
    unsigned n=bsd_answer(request,sizeof request,out.data);assert(n<=BSD_OUT && n>200 && out.data[0]==BSD_SSA_RSP);
    unsigned bytes=((unsigned)out.data[5]<<8)|out.data[6];assert(n==8+bytes);
    uint32_t t,h,len;assert(bl_de(out.data+7,bytes,&t,&h,&len) && t==6 && h+len==bytes);
    unsigned records=0;for(unsigned at=h;at<bytes;){
        uint32_t rt,rh,rl;assert(bl_de(out.data+7+at,bytes-at,&rt,&rh,&rl) && rt==6);
        at+=rh+rl;records++;
    }
    assert(records==3);
    for(unsigned i=0;i<16;i++)assert(out.guard[i]==0xa5);
    struct bl_sdp_info info;request[8]=0x11;request[9]=0x0f;
    n=bsd_answer(request,sizeof request,out.data);
    assert(bl_sdp_parse(out.data+7,n-8,0x110f,&info)==1 && info.has_class && info.psm==0x17);
    assert(info.profile_uuid==0x110e && info.features==1);
    puts("PASS: bounded all-record SDP browse and AVRCP Controller category-1 discovery alongside Audio Sink/Device ID");
}
static void scheduling(void)
{
    memset(br_links,0,sizeof br_links);memset(br_channels,0,sizeof br_channels);
    select_peer(0);select_peer(1);br_current=0;br_radio=0;br_switching=0;br_audio_link=0;ba_live=1;br_slice_at=0;
    uint16_t key[2][8];memcpy(key[0],(const void *)BR_FRAME()->cryptk,16);memcpy(key[1],(const void *)fm1_br_frame(bti_tab,1)->cryptk,16);
    bp_alarm(80);assert(!br_switching && br_radio==0 && br_current==0);
    bp_alarm(160);assert(br_switching && br_radio==0 && br_current==0);
    for(unsigned i=0;i<16;i++)assert(((uint16_t *)bti_tab)[i]&4096u);
    bp_alarm(165);assert(br_switching && br_radio==0);
    bp_alarm(166);assert(!br_switching && br_radio==1 && br_current==0);
    for(unsigned i=0;i<16;i++)assert((((uint16_t *)bti_tab)[i]&2047u)==FM1_BR_OFF_FRAME+180u);
    bp_alarm(174);assert(br_switching);bp_alarm(180);assert(br_radio==0 && !br_switching && br_current==0);
    assert(!memcmp(key[0],(const void *)BR_FRAME()->cryptk,16) && !memcmp(key[1],(const void *)fm1_br_frame(bti_tab,1)->cryptk,16));
    /* A standby's inquiry/page scan must not overwrite the current frame's
     * exchange mapping or crypto/RX/TX buffers. */
    uint8_t primary[180];memcpy(primary,BR_FRAME(),180);uint16_t exchange[16];memcpy(exchange,bti_tab,32);
    br_current=1;br_scan_program(0,100);assert(!memcmp(exchange,bti_tab,32));
    assert(!memcmp(primary,fm1_br_frame(bti_tab,0),180));
    brl.fhs_bit=200;fm1_br_frame(bti_tab,1)->rxbit=300;br_connection(BR_FRAME());
    assert(BR_FRAME()->rxptr[0]==2448 && BR_FRAME()->rxptr[1]==3128);
    assert(!memcmp(primary,fm1_br_frame(bti_tab,0),180));
    br_radio=br_current=1;br_switching=0;brl.central=1;brl.phase=BR_PAGE_SCAN;
    br_page_step=1;br_slice_at=0;br_schedule(80);assert(!br_switching && br_radio==1);
    puts("PASS: two DMA frames/keys, six-slot switch guard, playback-priority slices, IRQ context restoration and independent incoming RX banks");
}
static void playback_scheduling(void)
{
    /* Exercise the actual IRQ wrapper and alarm intervals over clock wrap,
     * both audio owners and empty/idle/negotiating second slots. Account for
     * inhibited guards as unavailable airtime, not as streaming time. */
    for(unsigned owner=0;owner<2;owner++)for(unsigned standby=0;standby<3;standby++){
        memset(br_links,0,sizeof br_links);memset(br_channels,0,sizeof br_channels);
        select_peer(0);select_peer(1);br_radio=(uint8_t)owner;br_audio_link=(uint8_t)owner;
        br_current=(uint8_t)(owner^1u);br_switching=0;ba_live=1;radio_buffered=1;
        if(standby!=1){brl.phase=BR_PAGE_SCAN;brl.encrypted=0;brl.pairing=standby==2;}
        uint32_t origin=BTI_CLK-200u;br_slice_at=origin;
        unsigned playing=0,away=0,longest_away=0,visits=0;
        for(unsigned at=0;at<16000u;){
            clock_now=(origin+at)&BTI_CLK;unsigned old=br_radio;
            bp_alarm(clock_now);assert(br_current==(owner^1u));
            if(old!=br_radio && br_radio!=owner)visits++;
            unsigned elapsed=alarm_slots;if(elapsed>16000u-at)elapsed=16000u-at;
            if(br_radio==owner && !br_switching){playing+=elapsed;away=0;}
            else {away+=elapsed;if(away>longest_away)longest_away=away;}
            at+=elapsed;
        }
        assert(playing>=16000u*(standby==2?84u:88u)/100u);
        assert(longest_away<=(standby==2?28u:20u));
        assert(visits>70u); /* Empty scan and the idle source remain reachable. */
    }
    /* Low prepared PCM delays a visit, but cannot starve the other client
     * forever. The six-slot exchange guard still applies at the hard bound. */
    select_peer(0);select_peer(1);br_current=br_radio=br_audio_link=0;
    br_switching=0;br_slice_at=0;radio_buffered=0;
    bp_alarm(160);bp_alarm(312);assert(!br_switching && br_radio==0);
    bp_alarm(320);assert(br_switching);bp_alarm(326);assert(br_radio==1 && !br_switching);
    radio_buffered=1;
    /* A control backlog gets the longer standby visit. A new accepted START
     * immediately changes the radio priority rather than waiting 160 slots. */
    br_current=1;br_slice_at=326;uint8_t pdu[17]={0};blt_put(pdu,17,3);
    br_schedule(334);assert(!br_switching);br_schedule(342);assert(br_switching);
    bp_alarm(348);assert(br_radio==0);br_current=1;br_stream_claim();
    br_current=0;br_schedule(356);assert(br_switching);bp_alarm(362);assert(br_radio==1);
    puts("PASS: 10-second/wrapped-clock radio models preserve 88% playback airtime and <=12.5-ms idle/empty visits, bounded low-PCM extensions, discovery/control and prompt ownership handover");
}
static void security(void)
{
    memset(br_links,0,sizeof br_links);memset(br_channels,0,sizeof br_channels);
    br_on=1;br_pairing=1;br_audio_link=255;br_switching=0;br_radio=0;
    for(unsigned i=0;i<BR_LINKS;i++){
        select_peer(i);brl.encrypted=0;brl.pairing=1;brl.key_kept=1;
        for(unsigned k=0;k<6;k++)brl.remote[k]=(uint8_t)(k+40+10*i);
        btp_begin(&brl_ssp,bti.own,brl.remote,br_rng,br_lmp_send,0);brl_ssp.peripheral=1;
        uint8_t key[16];for(unsigned k=0;k<16;k++)key[k]=(uint8_t)(20+40*i+k);
        btp_bond(&brl_ssp,key);
    }
    uint8_t random[16],sres[4],aco[2][12],kc[2][16],param[16];
    for(unsigned i=0;i<BR_LINKS;i++){
        br_current=(uint8_t)i;for(unsigned k=0;k<16;k++)random[k]=(uint8_t)(70+10*i+k);
        bts_e1(brl_ssp.lk,random,bti.own,sres,aco[i]);packet(BTL_AU_RAND,random,16);
        assert(brl_ssp.st==BTP_MODEREQ && !memcmp(brl_ssp.aco,aco[i],12));drain();
    }
    for(unsigned i=0;i<BR_LINKS;i++){
        br_current=(uint8_t)i;
        param[0]=BTL_ENCRYPTION_MODE_REQ;packet_tid(BTL_ACCEPTED,1,param,1);drain();assert(brl_ssp.st==BTP_KEYSIZE);
        param[0]=16;packet_tid(BTL_ENCRYPTION_KEY_SIZE_REQ,1,param,1);drain();
        for(unsigned k=0;k<16;k++)param[k]=(uint8_t)(100+10*i+k);
        bts_e3(brl_ssp.lk,param,aco[i],kc[i]);packet_tid(BTL_START_ENCRYPTION_REQ,1,param,16);
        assert(brl.encrypted && brl_ssp.st==BTP_ENCRYPTED && !memcmp(kc[i],brl_ssp.kc,16));
        for(unsigned k=0;k<8;k++)assert(BR_FRAME()->cryptk[k]==(kc[i][2*k]|kc[i][2*k+1]<<8));
        assert((BR_FRAME()->linkcntl[0]&0x8080)==0x8080 && (BR_FRAME()->linkcntl[1]&0x8080)==0x8080);
        drain();
    }
    assert(memcmp(kc[0],kc[1],16) && !br_pairing && br_links[0].encrypted && br_links[1].encrypted);
    for(unsigned i=0;i<BR_LINKS;i++)for(unsigned k=0;k<8;k++)
        assert(fm1_br_frame(bti_tab,i)->cryptk[k]==(kc[i][2*k]|kc[i][2*k+1]<<8));
    puts("PASS: interleaved saved-bond authentication/encryption keeps both ACOs, derived keys, encrypted directions, queues and peer state independent");
}
static void lifecycle(void)
{
    memset(br_links,0,sizeof br_links);memset(br_channels,0,sizeof br_channels);
    select_peer(0);select_peer(1);br_current=0;br_on=1;br_pairing=0;br_radio=0;br_switching=0;
    br_pair(1);assert(!br_pairing && br_links[0].encrypted && br_links[1].encrypted);
    br_current=1;br_drop(0,BR_FAIL_REMOTE,0x13);br_pair(1);
    assert(br_pairing && br_links[0].encrypted && !br_links[1].reconnect_hold);
    br_pair(0);br_current=0;br_drop(0,BR_FAIL_REMOTE,0x13);
    assert(br_switching);clock_now=br_slice_at+6;bp_alarm(clock_now);
    assert(br_switching && br_links[0].closing && br_links[1].closing);
    fm1_ms+=301;bp_poll();assert(!br_links[0].closing && !br_links[1].closing);
    assert(br_links[0].phase==BR_PAGE_SCAN && br_links[1].phase==BR_PAGE_SCAN);
    assert(!br_links[0].encrypted && !br_links[1].encrypted && br_links[0].reconnect_hold);
    bp_alarm(clock_now+8);assert(!br_switching);
    /* Per-peer diagnostics remain bounded and refer to the selected stream,
     * regardless of which frame the IRQ last served. */
    br_audio_link=1;br_current=0;br_channels[1].profile.codec=BTR_CODEC_SBC;
    const char *line;for(unsigned row=0;(line=br_debug_line(row));row++)assert(strlen(line)<=43 && row<48);
    assert(br_current==0);
    puts("PASS: pairing preserves a playing peer, a full pair stays undiscoverable, simultaneous detach drains before reset and both slots return to scan; bounded diagnostics restore context");
}
static void manual_holds(void)
{
    br_held_valid=br_held_next=0;memset(br_links,0,sizeof br_links);memset(br_channels,0,sizeof br_channels);
    select_peer(0);select_peer(1);br_current=0;br_radio=1;br_switching=0;br_on=1;busy=1;
    bta.nmem=2;for(unsigned i=0;i<2;i++){
        memcpy(bta.mem[i].addr,br_links[i].remote,6);bta.mem[i].bonded=1;
    }
    br_drop(0,BR_FAIL_REMOTE,0x13);assert(br_peer_held(bta.mem[0].addr) && !br_peer_held(bta.mem[1].addr));
    /* Neither free slot may undo peer 0's manual disconnect. The other peer's
     * actual range loss must still be eligible for automatic reconnect. */
    br_current=1;brl.last_good=fm1_ms-4001;br_drop(0,BR_FAIL_RX,0);assert(!br_peer_held(bta.mem[1].addr));
    br_current=0;brl.closing=0;brl.phase=BR_PAGE_SCAN;brl.central=0;brl.pairing=0;
    br_retry_at=fm1_ms;br_retry_index=0;br_reconnect_poll();
    assert(brl.central && !memcmp(brl.remote,bta.mem[1].addr,6));
    /* Bond MRU order and slot reassignment must not move a hold to the wrong
     * address. An explicit successful connection releases just its hold. */
    struct bta_mem swap=bta.mem[0];bta.mem[0]=bta.mem[1];bta.mem[1]=swap;
    assert(!br_peer_held(bta.mem[0].addr) && br_peer_held(bta.mem[1].addr));
    memcpy(brl.remote,bta.mem[1].addr,6);br_hold_peer(0);assert(!br_peer_held(brl.remote));
    br_hold_peer(1);br_pair(1);assert(!br_peer_held(brl.remote));
    assert(brl.pairing && brl.closing && brl.central); /* page cancelled, no DMA reset yet */
    br_current=1;brl.closing=0;brl.central=0;brl.phase=BR_LINKED;brl.encrypted=brl.setup=1;
    brl.started=brl.last_good=fm1_ms;
    br_current=0;fm1_ms+=301;bp_poll();
    assert(brl.pairing && !brl.closing && !brl.central && brl.phase==BR_INQUIRY);
    assert(br_links[1].encrypted && br_links[1].setup);
    puts("PASS: manual holds follow addresses across both slots/bond reordering, do not block another peer's range-loss reconnect, and clear on explicit connect/pairing");
}
static void profile_disconnects(void)
{
    for(unsigned leaving=0;leaving<2;leaving++)for(unsigned avrcp_first=0;avrcp_first<2;avrcp_first++){
        memset(br_links,0,sizeof br_links);memset(br_channels,0,sizeof br_channels);br_held_valid=0;
        select_peer(0);select_peer(1);br_current=(uint8_t)leaving;br_radio=(uint8_t)(leaving^1u);
        br_audio_link=br_radio;br_switching=0;ba_live=1;br_on=1;busy=1;fm1_ms=3000;
        brl.secure_at=0;btr_phase=789;
        for(unsigned step=0;step<2;step++){
            unsigned c=(step==avrcp_first)?1:3;
            uint8_t p[]={8,0,1,0,BL_DISC_REQ,42,4,0,(uint8_t)(64+c),0,(uint8_t)brc.ch[c].remote,0};
            acl(p,sizeof p,2);drain();
            uint32_t queued=blt.w;br_avrcp_poll();assert(blt.w==queued);
        }
        assert(br_peer_held(brl.remote) && brl.reconnect_hold && brl.encrypted && !brc.ch[1].remote);
        assert(ba_live && btr_phase==789 && br_audio_link==(leaving^1u));
        /* An accepted incoming encrypted signalling open can explicitly
         * reconnect on the same ACL; only that peer's hold is cleared. */
        uint8_t conn[]={8,0,1,0,BL_CONN_REQ,43,4,0,0x19,0,110,0};
        acl(conn,sizeof conn,2);drain();assert(brc.ch[1].remote==110 && !brl.reconnect_hold && !br_peer_held(brl.remote));
        uint32_t queued=blt.w;br_avrcp_poll();assert(blt.w>queued);drain();
        /* A DETACH at the FIFO head is terminal even with both the outgoing
         * FIFO and the retained control response blocked. No false RX timeout. */
        uint8_t pdu[17]={0};for(unsigned i=0;i<BLT_Q;i++)assert(blt_put(pdu,17,3));
        uint8_t echo[]={4,0,1,0,8,44,0,0};acl(echo,sizeof echo,2);assert(brc.pending);
        struct bti_rx r={.kind=BTI_K_LMP,.link=(uint8_t)leaving,.generation=brl.generation,.len=2,.data={BTL_DETACH<<1,0x13}};
        assert(br_rx_ready(&r));bp_print_rx(&r);
        assert(br_peer_held(brl.remote) && br_fault.reason==BR_FAIL_REMOTE && br_fault.detail==0x13 && !brc.pending);
        assert(ba_live && btr_phase==789 && br_links[leaving^1u].encrypted);
    }
    puts("PASS: both AVRCP/AVDTP close orders preserve manual-disconnect holds without reopening profiles, explicit incoming reconnect works, and DETACH bypasses blocked replies without disturbing the playing peer");
}
int main(void){arbitration();backpressure();discovery();scheduling();playback_scheduling();security();lifecycle();manual_holds();profile_disconnects();return 0;}
