/* SPDX-License-Identifier: MPL-2.0 */
/* Classic incoming link for the gateway. Reuses the audited radio initialisation,
 * table layout, RX ring, TX handover and SSP. Incoming page timing requires FM-1
 * validation: the host emulator has no BR/EDR packet engine. */
#include "bt_lmp.c"
#include "bt_sha256.c"
#include "bt_safer.c"
#include "bt_ssp.c"
#include "bt_receiver_pcm.c"
#include "bt_receiver_media.c"
#define BLT_CLKN() bti_clkn()
#define BLT_AFH_MAP(w) fm1_br_afh_map(0,(w))
#define BLT_OFF_TX0 4272u
#define BLT_OFF_TX1 4292u
#define BLT_MEDIA_READY() 0
#define BLT_MEDIA_LOAD(f,t,i) ((void)0)
static void br_note_tx(const uint8_t *p,uint32_t n,uint32_t llid);
#define BLT_NOTE_SIG(llid) br_note_tx(blt.q[blt.r%BLT_Q],blt.ql[blt.r%BLT_Q],(llid))
#include "bt_ltx.c"
#include "bt_receiver_page.h"
static struct btp brl_ssp;
static void br_audio_flush(uint32_t rate)
{
    /* The worker must finish before reset can invalidate its ring publication. */
    btr_media_reset();uint32_t f=irq_save();btr_audio_reset(rate);irq_restore(f);
}
static int br_tx(const uint8_t *p,uint32_t n)
{
    uint32_t at,m;
    if((n+BTL_MAX-1)/BTL_MAX>BLT_Q-(blt.w-blt.r))return 0;
    for(at=0;at<n;at+=m){m=n-at>BTL_MAX?BTL_MAX:n-at;blt_put(p+at,m,at?1:2);}
    return 1;
}
#define BTR_SEND(p,n) br_tx((p),(n))
#define BTR_FLUSH(rate) br_audio_flush(rate)
#define BTR_SECURE() (brl.encrypted)
#include "bt_receiver_l2cap.c"
static void br_lmp_send(void *ctx,const uint8_t *p,uint32_t n) {(void)ctx;blt_put(p,n,3);}
static int br_rng(uint8_t *p,unsigned n)
{
    while(n){uint32_t v=fm1_ble_rng32()^fm1_ticks()^brl.noise;for(unsigned i=0;i<4&&n;i++,n--)*p++=(uint8_t)(v>>(8*i));}
    return 1;
}
#include "bt_receiver_page.c"
#include "bt_receiver_debug.h"
/* Main-thread diagnostics only: no formatting/logging in the radio ISR. */
static void br_diag(const char *label,uint32_t value)
{
    char text[64],*p=text;
    while(*label && p<text+48)*p++=*label++;
    p=put_dec(p,value);*p=0;shim_log(text);
}
static void br_drop(int notify,uint32_t reason,uint32_t detail)
{
    uint8_t p[2]={(uint8_t)((BTL_DETACH<<1)|!brl.central),0x13};
    if(brl.phase>=BR_CONNECTING && !brl.closing)br_capture_fault(reason,detail);
    if (notify && bti_busy() && brl.phase >= BR_CONNECTING) {
        if (!brl.closing) {
            blt_put(p,2,3);
            fm1_br_irqs_hold(1);blt_publish();blt_fill(fm1_br_frame(bti_tab,0),bti_tab);fm1_br_irqs_hold(0);
            brl.closing=1;brl.close_at=fm1_ms;
        }
    } else {
        fm1_br_irqs_hold(1);bti_run_stop();fm1_br_irqs_hold(0);brl.closing=0;
    }
    /* 0x13 is Remote User Terminated Connection. Do not undo a source's
     * deliberate Disconnect by paging it again. Early setup handovers can
     * also use 0x13, so require an established encrypted session. Missing-key
     * failures need explicit pairing, rather than endless saved-key retries. */
    if((reason==BR_FAIL_REMOTE && detail==0x13 && brl.encrypted) ||
       (reason==BR_FAIL_SSP && detail==6))brl.reconnect_hold=1;
    br_reset();brl.encrypted=brl.setup=brl.key_kept=0;
    /* The next poll must not recapture this detached link as an auth timeout. */
    brl.central=0;br_retry_at=fm1_ms+10000;
    brl.phase=BR_PAGE_SCAN;
}
static void br_pair(int on)
{
    if(!brl.on)return;
    if(on)brl.reconnect_hold=0;
    brl.pairing=(uint8_t)(on!=0);
    if(brl.central && brl.phase<BR_CONNECTING){
        fm1_br_irqs_hold(1);bti_run_stop();fm1_br_irqs_hold(0);
        br_retry_at=fm1_ms+10000;br_listen();return;
    }
    if(on && brl.phase>=BR_CONNECTING)br_drop(1,BR_FAIL_USER,0);
    if(!brl.closing && bti_busy() && brl.phase<BR_CONNECTING){
        fm1_br_irqs_hold(1);br_scan_program(brl.pairing,bti_clkn());fm1_br_irqs_hold(0);
    }
}
static void bp_print_rx(const struct bti_rx *r)
{
    uint8_t out[BTL_MAX+8];uint32_t n,tid=0,op;
    if(!brl.on || brl.closing || !bti_busy() || brl.phase<BR_CONNECTING)return;
    if(r->kind==BTI_K_CONN) {
        shim_log(brl.central?"BT reconnect: ACL connected":"BT incoming ACL connected");
        br_reset();brl.encrypted=brl.setup=brl.setup_sent=brl.key_kept=0;
        brd.trace_w=brd.last_op=brd.sent=brd.last_tx=0;
        struct bta_mem *m=bta_mem_of(brl.remote);
        brl.allowed=brl.pairing;
        uECC_set_rng(br_rng);btp_begin(&brl_ssp,bti.own,brl.remote,br_rng,br_lmp_send,0);brl_ssp.peripheral=!brl.central;
        if(m&&m->bonded){btp_bond(&brl_ssp,m->lk);brl_ssp.init=brl.central;}
        else if(!brl.allowed){shim_log("BT unknown source: enable pairing with OCT+");br_drop(1,BR_FAIL_NOT_PAIRING,0);return;}
        n=btl_features_req(out);out[0]|=!brl.central;br_lmp_send(0,out,n);
        if(brl.central){n=btl_version_req(out);br_lmp_send(0,out,n);n=btl_host_conn_req(out);br_lmp_send(0,out,n);}
        return;
    }
    /* SDP and basic L2CAP control are available before authentication. The
     * L2CAP adapter separately gates AVDTP connection/data on encryption. */
    if(r->kind==BTI_K_ACL) {br_acl(r->idx,r->data,r->len);return;}
    if(r->kind!=BTI_K_LMP)return;
    op=btl_opcode(r->data,r->len,&tid);
    if(op==(BTL_ESCAPE<<8|BTL_X_IO_CAPABILITY_REQ) && !brl.allowed) {br_drop(1,BR_FAIL_NOT_PAIRING,op);return;}
    uint32_t before=brl_ssp.st,handled=btp_in(&brl_ssp,r->data,r->len);
    br_trace_note(op,tid,before,brl_ssp.st);
    /* btp_in returns 0 for NOT_ACCEPTED even though it sets FAILED. */
    if(brl_ssp.st==BTP_FAILED){br_diag("BT LMP failed op ",op);br_diag(brl_ssp.reason==6?"BT no key; pair OCT+ ":"BT auth error ",brl_ssp.reason);br_drop(1,BR_FAIL_SSP,brl_ssp.reason);return;}
    if(brl.central && brl_ssp.st==BTP_STARTING && before!=BTP_STARTING){
        struct fm1_br_frame *f=fm1_br_frame(bti_tab,0);fm1_br_irqs_hold(1);
        for(unsigned i=0;i<8;i++)f->cryptk[i]=(uint16_t)(brl_ssp.kc[2*i]|(uint16_t)brl_ssp.kc[2*i+1]<<8);
        for(unsigned i=0;i<2;i++)f->linkcntl[i]|=0x8000;fm1_br_irqs_hold(0);
    }
    if(handled) {
        if(before==BTP_AUTHING && brl_ssp.st==BTP_AUTHED)
            shim_log(brl_ssp.dev_authed?"BT mutual authentication complete":"BT saved bond authentication complete");
        if(before!=BTP_AUTHED && brl_ssp.st==BTP_AUTHED &&
            (op==BTL_SRES || op==BTL_AU_RAND)) {
            brl.auth_at=fm1_ms;
            if(!brl.central || brl_ssp.dev_authed){
                if(brl.central)btp_i_encrypt(&brl_ssp);else btp_peripheral_encrypt(&brl_ssp);
                shim_log("BT encryption requested");
            }
        }
        if(brl_ssp.st==BTP_ENCRYPTED && !brl.encrypted) {
            struct fm1_br_frame *f=fm1_br_frame(bti_tab,0);
            fm1_br_irqs_hold(1);
            for(uint32_t i=0;i<8;i++)f->cryptk[i]=(uint16_t)(brl_ssp.kc[2*i]|brl_ssp.kc[2*i+1]<<8);
            for(uint32_t i=0;i<2;i++)f->linkcntl[i]|=0x8080;
            fm1_br_irqs_hold(0);brl.encrypted=1;brl.pairing=0;brl.reconnect_hold=0;brl.secure_at=fm1_ms;
            shim_log("BT encryption active");
            if(!brl.key_kept){bta_remember(brl.remote,"AUDIO SOURCE");memcpy(bta.mem[0].lk,brl_ssp.lk,16);bta.mem[0].bonded=1;bta.key_new=1;brl.key_kept=1;}
        }
        return;
    }
    if(brl.central && op==BTL_ACCEPTED && r->len>=2 && r->data[1]==BTL_HOST_CONN_REQ && !brl.setup_sent){
        n=btl_setup_complete(out);br_lmp_send(0,out,n);brl.setup_sent=1;return;
    }
    if(op==BTL_DETACH){uint32_t reason=r->len>1?r->data[1]:0;br_diag("BT remote detached, reason ",reason);br_drop(0,BR_FAIL_REMOTE,reason);return;}
    if(op==BTL_HOST_CONN_REQ) {
        out[0]=(BTL_ACCEPTED<<1)|(uint8_t)tid;out[1]=BTL_HOST_CONN_REQ;br_lmp_send(0,out,2);return;
    }
    if(op==BTL_SETUP_COMPLETE && !brl.setup){
        if(!brl.setup_sent){n=btl_setup_complete(out);out[0]|=!brl.central;br_lmp_send(0,out,n);brl.setup_sent=1;}brl.setup=1;shim_log("BT LMP setup complete");
        /* Android can wait for service security without sending AU_RAND.
         * Start saved-key authentication once, in our Peripheral transaction. */
        /* Explicit pairing also repairs a peer that forgot its key. Wait for
         * its IO capability request instead of racing fresh SSP with an old
         * AU_RAND. The stored key can still answer a valid peer challenge. */
        if(brl_ssp.bonded && brl_ssp.st==BTP_PAIRED && !brl.pairing){btp_auth(&brl_ssp);shim_log("BT saved bond authentication requested");}
        return;
    }
    n=btl_answer(r->data,r->len,BTR_NAME,out);if(n)br_lmp_send(0,out,n);
}
static void bp_poll(void)
{
    static uint32_t diag_at,diag_pages,diag_fhs;
    if(fm1_ms-diag_at>=1000 && (diag_pages!=brl.pages || diag_fhs!=brl.fhs)) {
        diag_at=fm1_ms;diag_pages=brl.pages;diag_fhs=brl.fhs;
        /* One compact scan line; do not push pairing failures out of the log. */
        char line[40]="BT scan ",*p=put_dec(line+8,diag_pages);*p++='/';p=put_dec(p,diag_fhs);*p=0;shim_log(line);
    }
    if(brl.closing) {
        if(fm1_ms-brl.close_at<300)return;
        fm1_br_irqs_hold(1);bti_run_stop();fm1_br_irqs_hold(0);brl.closing=0;
    }
    if(!brl.on)return;
    if(brl.phase>=BR_CONNECTING && ((int32_t)(fm1_ms-brl.last_good)>4000 || (!brl.encrypted && fm1_ms-brl.started>30000))) {
        uint32_t reason=(int32_t)(fm1_ms-brl.last_good)>4000?BR_FAIL_RX:BR_FAIL_AUTH;
        br_diag("BT timeout at phase ",brl.phase);br_diag("BT security state ",brl_ssp.st);br_drop(0,reason,0);
    }
    br_reconnect_poll();
    if(!bti_busy()){br_listen();return;}
    /* Give a Peripheral's reverse AU_RAND time to arrive before negotiating
     * the encryption ACO. Some bonded sources only answer our challenge. */
    if(brl.central && brl_ssp.st==BTP_AUTHED && fm1_ms-brl.auth_at>=2000){
        btp_i_encrypt(&brl_ssp);shim_log("BT encryption requested");
    }
    if(brl.central && brl.setup && brl.encrypted && !brc.profile_ready){
        if(fm1_ms-brl.secure_at>=1000)br_connect_signal();
        if(fm1_ms-brl.secure_at>=15000 && brc.profile.state==BTR_IDLE){
            shim_log("BT reconnect: audio profile timed out");br_drop(1,BR_FAIL_PROFILE,0);return;
        }
    }
    if(bti.mode==BTI_M_CONN) {
        fm1_br_irqs_hold(1);blt_publish();blt_fill(fm1_br_frame(bti_tab,0),bti_tab);fm1_br_irqs_hold(0);
    }
}
static void bp_sec_tick(void) {}
static void bta_saw(const uint8_t a[6],uint32_t c) {(void)a;(void)c;}
static void bta_named(const uint8_t a[6],const char *n) {(void)a;(void)n;}
