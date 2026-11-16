/* SPDX-License-Identifier: MPL-2.0 */
/* Receiver backend of bt_own_audio.c. Classic only: BLE advertising stays off. */
#undef BTI_RING
#define BTI_RING 32u
#include "bt_audio.h"
#include "bta_mem.c"
static int own_bt_trims(struct ble_rf_trims *t) {*t=OWN_TRIMS;fm1_rf_cal_on=1;return 1;}
#define BTI_RADIO_TRIMS own_bt_trims
#include "bt_inq.c"
#pragma pop_macro("FELUCCA_CDC")
#include "../bt/br/bt_receiver_ui.h"
static void own_bta_load(void)
{
    memcpy(bta.own,own.bta_own,6);bta.own_set=own.bta_own_set;
    memcpy(bta.mem,own.bta_mem,sizeof bta.mem);bta.nmem=own.bta_nmem<=BTA_MEMS?own.bta_nmem:0;
}
static void own_bta_keep(void)
{
    if(!bta.key_new)return;bta.key_new=0;
    memcpy(own.bta_own,bta.own,6);own.bta_own_set=bta.own_set;memcpy(own.bta_mem,bta.mem,sizeof bta.mem);own.bta_nmem=bta.nmem;own_dirty=1;
}
static void own_bta_poll(void) {
#ifdef APP_BT_RESAMPLE_CPU2
    btr_worker_check();
    btr_resample_poll();
#endif
#ifdef APP_BT_SBC_CPU2
    btr_sbc_poll();
#endif
#ifdef APP_BT_APTX
    btr_aptx_poll();
#endif
#ifdef APP_BT_LDAC
    btr_ldac_poll();
#endif
#ifdef APP_BT_AAC
    btr_aac_poll();
#endif
    bp_poll();
}
int plat_bt_receiver_on(int on)
{
    if(on) {
        plat_ble_enable(0);ble_start();
        if(!own_bta_attached) {uint32_t f=irq_save();fm1_guard_unlock_top();bti_attach();fm1_guard_lock_top();irq_restore(f);own_bta_attached=1;}
#ifdef APP_BT_MULTIPOINT
        if(!br_on){
            br_on=1;br_pairing=0;br_audio_link=255;br_held_valid=br_held_next=0;
            for(unsigned i=0;i<BR_LINKS;i++){
                br_current=(uint8_t)i;br_reset();brl.on=1;brl.pairing=brl.closing=0;
                brl.reconnect_hold=0;br_retry_at=fm1_ms+3000u+1000u*i;br_retry_index=(uint8_t)i;br_listen();
            }
            br_current=0;
        }
#else
        if(!brl.on){br_reset();br_retry_at=fm1_ms+3000;br_retry_index=0;brl.reconnect_hold=0;brl.on=1;brl.pairing=0;br_listen();}
#endif
        return bti_busy()?0:-1;
    }
#ifdef APP_BT_MULTIPOINT
    br_on=br_pairing=0;
    fm1_br_irqs_hold(1);bti_run_stop();fm1_br_irqs_hold(0);
    for(unsigned i=0;i<BR_LINKS;i++){
        br_current=(uint8_t)i;brl.on=brl.pairing=brl.closing=0;brl.generation++;br_reset();
    }
    br_audio_link=255;br_current=0;
#else
    if(brl.on){brl.on=0;brl.pairing=0;br_drop(1,BR_FAIL_USER,0);}
#endif
    return 0;
}
void plat_bt_receiver_pair(int on)
{
    br_pair(on);
}
int plat_bt_receiver_pairing(void) {
#ifdef APP_BT_MULTIPOINT
    return br_on && br_pairing;
#else
    return brl.on&&brl.pairing;
#endif
}
int plat_bt_receiver_connected(void) {
    for(unsigned i=0;i<BR_LINKS;i++)if(br_links[i].on && !br_links[i].closing && br_links[i].encrypted &&
        br_links[i].setup && br_links[i].phase==BR_LINKED)return 1;
    return 0;
}
int plat_bt_receiver_codec(void)
{
    unsigned selected=0;
#ifdef APP_BT_MULTIPOINT
    selected=br_selected_link();
#endif
    const struct btr_profile *profile=&br_channels[selected].profile;
    if(!plat_bt_receiver_connected() || profile->state==BTR_IDLE)return PLAT_BT_CODEC_NONE;
    return profile->codec==BTR_CODEC_SBC?PLAT_BT_CODEC_SBC:
        profile->codec==BTR_CODEC_AAC?PLAT_BT_CODEC_AAC:
        profile->codec==BTR_CODEC_APTX?PLAT_BT_CODEC_APTX:profile->codec==BTR_CODEC_APTX_HD?PLAT_BT_CODEC_APTX_HD:profile->codec==BTR_CODEC_LDAC?PLAT_BT_CODEC_LDAC:PLAT_BT_CODEC_NONE;
}
unsigned plat_bt_receiver_graphs(void)
{
    int codec=plat_bt_receiver_codec();
    if(codec==PLAT_BT_CODEC_AAC)return 0;
#ifdef APP_BT_LDAC
    if(codec==PLAT_BT_CODEC_LDAC && !btr_ldac)return 0;
#endif
    return 3;
}
void plat_bt_receiver_mix(int32_t *lr,uint32_t n,uint32_t gain) {if(brl.on)btr_pcm_mix(lr,n,gain);}
static int own_bta_feed(const int32_t *o,uint32_t n) {(void)o;(void)n;return 0;}
static int bta_frame(const uint8_t *p,uint32_t n) {(void)p;(void)n;return 0;}
int plat_bta_job_pending(void) {return 0;}
void plat_bta_job(void) {}
void plat_bta_job_keep(int on) {(void)on;}
uint32_t plat_bta_job_inline(void) {return 0;}

void plat_bt_receiver_preference(int preference)
{
    if(preference>=0 && preference<=2)plat_bt_receiver_codecs(preference==1?2:preference==2?1:3);
}
void plat_bt_receiver_codecs(unsigned mask)
{
    if(!mask || mask>31 || btr_codec_mask==mask)return;
    btr_codec_mask=(uint8_t)mask;
#ifdef APP_BT_MULTIPOINT
    uint8_t saved=br_current;
    for(unsigned i=0;i<BR_LINKS;i++){
        br_current=(uint8_t)i;
        if(brl.on && brl.phase>=BR_CONNECTING)br_drop(1,BR_FAIL_USER,0);
    }
    br_current=saved;
#else
    if(brl.on && brl.phase>=BR_CONNECTING)br_drop(1,BR_FAIL_USER,0);
#endif
}
