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
        if(!brl.on){br_reset();br_retry_at=fm1_ms+3000;br_retry_index=0;brl.reconnect_hold=0;brl.on=1;brl.pairing=0;br_listen();}
        return bti_busy()?0:-1;
    }
    if(brl.on){brl.on=0;brl.pairing=0;br_drop(1,BR_FAIL_USER,0);}
    return 0;
}
void plat_bt_receiver_pair(int on)
{
    br_pair(on);
}
int plat_bt_receiver_pairing(void) {return brl.on&&brl.pairing;}
int plat_bt_receiver_connected(void) {return brl.on&&brl.encrypted&&brl.setup&&brl.phase==BR_LINKED;}
int plat_bt_receiver_codec(void)
{
    if(!plat_bt_receiver_connected() || brc.profile.state==BTR_IDLE)return PLAT_BT_CODEC_NONE;
    return brc.profile.codec==BTR_CODEC_SBC?PLAT_BT_CODEC_SBC:
        brc.profile.codec==BTR_CODEC_AAC?PLAT_BT_CODEC_AAC:PLAT_BT_CODEC_NONE;
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
    if(preference<0 || preference>2 || btr_codec_preference==preference)return;
    btr_codec_preference=(uint8_t)preference;
    if(brl.on && brl.phase>=BR_CONNECTING)br_drop(1,BR_FAIL_USER,0);
}
