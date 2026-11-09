/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static void tap(int b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void turn(int k,int d){enc_acc[k]=d;app_input_only();}
static uint32_t badge_hash(void)
{
    uint32_t h=2166136261u;
    for(unsigned y=0;y<16;y++)for(unsigned x=38;x<91;x++)h=(h^fb[y*240+x])*16777619u;
    return h;
}
static void codec_badges(void)
{
    uint32_t blank=badge_hash(),sbc;
    assert(!plat_bt_receiver_codec());
    btr_host_codec=PLAT_BT_CODEC_SBC;now_ms+=250;app_loop();sbc=badge_hash();
    assert(gateway_codec_seen==PLAT_BT_CODEC_SBC && sbc!=blank);
    uint32_t count=blits;app_loop();assert(blits==count);
    btr_host_codec=PLAT_BT_CODEC_AAC;now_ms+=250;app_loop();
    assert(gateway_codec_seen==PLAT_BT_CODEC_AAC && badge_hash()!=sbc && gateway_aac_active && !gateway_display_block);
    tap(B_EDIT);app_loop();assert(gateway_aac_active && !gateway_display_block);
    btr_host_connected=0;app_loop();assert(!gateway_codec_seen && fb[2*240+38]==swap16(RGB(190,25,25)));
    plat_bt_receiver_on(0);app_loop();tap(B_HOME);
    plat_bt_receiver_on(1);btr_host_connected=1;btr_host_codec=0;now_ms+=250;app_loop();
    assert(!gateway_codec_seen && badge_hash()==blank);
    btr_host_connected=0;app_loop();
    puts("PASS: combined BT/codec badge on all screens, 250ms codec refresh, disconnect red and AAC display arbitration");
}
static void mixed_audio(void)
{
    static int32_t usb[2*APP_BLOCK],out[2*APP_BLOCK];
    static int16_t bt[512];uint32_t peak[2];
    for(unsigned i=0;i<APP_BLOCK;i++){usb[2*i]=1000000;usb[2*i+1]=-500000;bt[2*i]=8000;bt[2*i+1]=-4000;}
    usb_play_buf=usb;usb_play_n=APP_BLOCK;plat_usb_play_gain(4096);plat_master_meter_enable(1);
    plat_bt_receiver_on(1);plat_master_mute_init(0);
    for(unsigned full=0;full<2;full++)for(unsigned pot=0;pot<=4096;pot+=2048) {
        master=pot;uac_full=full;usb_play_at=0;btr_audio_reset(44100);
        for(unsigned i=0;i<BTR_TARGET/256;i++)btr_pcm_put(bt,256);
        plat_master_peaks(peak);sim_render(out);plat_master_peaks(peak);
        int expected=(int)(3048000LL*APP_MASTER_FX_GAIN(pot)/4096);
        assert(abs(out[2*FX_DELAY]-expected)<100 && abs(out[2*FX_DELAY+1]+expected/2)<100);
        assert(abs((int)peak[0]-expected)<100); /* master applied exactly once to BOTH sources */
    }
    master=4096;uac_full=0;plat_master_mute_init(1);usb_play_at=0;btr_audio_reset(44100);
    for(unsigned i=0;i<BTR_TARGET/256;i++)btr_pcm_put(bt,256);
    sim_render(out);plat_master_peaks(peak);assert(!out[0] && peak[0]>3047900); /* combined meter before mute */
    plat_master_mute_init(0);
    for(unsigned i=0;i<APP_BLOCK;i++){usb[2*i]=8000000;usb[2*i+1]=-8000000;}
    usb_play_at=0;btr_audio_reset(44100);
    for(unsigned i=0;i<BTR_TARGET/256;i++)btr_pcm_put(bt,256);
    sim_render(out);assert(out[2*FX_DELAY]==8388607 && out[2*FX_DELAY+1]==-8388608); /* combined sources saturate, never wrap */
    usb_play_buf=0;plat_bt_receiver_on(0);
    memset(out,0,sizeof out);plat_bt_receiver_mix(out,APP_BLOCK,4096);assert(!out[0]);
    puts("PASS: USB + Bluetooth stereo mix, MASTER once, pre-mute meter, mute and saturation");
}
int main(void)
{
    palette_set(APP_PALETTE);
    uint32_t writes;gateway_settings_t record;
    now_ms=100;app_init();
    /* Exercise OFF -> ON drafts explicitly; fresh-install ON is covered by parameters_test. */
    gateway_bt_mode=gateway_bt_draft=0;gateway_saved=gateway_settings();
    app_ui_init();app_loop();
    assert(!gateway_bt_mode && !gateway_save_notice && !btr_host_on && !(dim_btn&(1u<<B_SAVE)));
    tap(B_EDIT);app_loop();assert(gateway_screen==GATEWAY_SETTINGS && dim_btn&(1u<<B_SAVE));
    assert(fb[45*240+10]);writes=store_writes;
    turn(EN_ALGO,INT_MAX);assert(gateway_bt_draft && !gateway_bt_mode && !btr_host_on);
    now_ms+=2000;app_loop();assert(store_writes==writes); /* never autosave draft */
    turn(EN_SELECT,INT_MAX);assert(gateway_settings_row==GATEWAY_SETTING_ROWS-1);turn(EN_SELECT,INT_MIN);assert(!gateway_settings_row);
    tap(B_PLAY);now_ms+=750;app_loop();
    assert(plat_store_load(OBJ_SETTINGS,&record,sizeof record)==sizeof record && record.bluetooth==0);
    store_fail=1;tap(B_SAVE);app_loop();assert(gateway_save_notice==2 && !gateway_bt_mode && !btr_host_on);
    store_fail=0;tap(B_SAVE);app_loop();assert(gateway_save_notice==1 && gateway_bt_mode && btr_host_on);
    assert(dim_btn&(1u<<B_OCTUP));
    uint32_t blit=blits;now_ms+=1000;app_loop();assert(gateway_save_notice==1 && blits<=blit+1);
    /* 1-second threshold, hold fires once, next press cancels without rearming while held. */
    held_btn=1u<<B_OCTUP;app_input_only();now_ms+=999;app_input_only();assert(!btr_host_pairing);
    now_ms++;app_input_only();assert(btr_host_pairing && lit_btn&(1u<<B_OCTUP) && !(dim_btn&(1u<<B_OCTUP)));
    now_ms+=3000;app_input_only();assert(btr_host_pairing);
    held_btn=0;app_input_only();held_btn=1u<<B_OCTUP;app_input_only();assert(!btr_host_pairing);
    now_ms+=2000;app_input_only();assert(!btr_host_pairing);held_btn=0;app_input_only();
    turn(EN_ALGO,-1);assert(!gateway_save_notice && !gateway_bt_draft && gateway_bt_mode);
    tap(B_HOME);app_loop();assert(!gateway_save_notice && !(dim_btn&(1u<<B_SAVE)));
    tap(B_EDIT);assert(gateway_bt_draft); /* abandoning a draft restores applied value */
    tap(B_SAVE);assert(gateway_save_notice==1);
    app_init();app_boot();app_ui_init();assert(gateway_bt_mode && btr_host_on && !gateway_save_notice && !btr_host_pairing);
    /* A failed/guarded boot stays quiet through UI init; explicit SAVE can retry. */
    plat_bt_receiver_on(0);btr_host_start_fail=1;
    app_init();app_boot();app_ui_init();assert(gateway_bt_mode && !gateway_bt_running && !btr_host_on);
    tap(B_EDIT);tap(B_SAVE);assert(gateway_save_notice==2 && !gateway_bt_running);
    btr_host_start_fail=0;tap(B_SAVE);assert(gateway_save_notice==1 && gateway_bt_running && btr_host_on);
    tap(B_HOME);
    plat_bt_receiver_on(0);app_init();app_ui_init(); /* platform suppresses app_boot after a radio crash */
    assert(gateway_bt_mode && !gateway_bt_running && !btr_host_on);
    tap(B_EDIT);tap(B_SAVE);tap(B_HOME);assert(gateway_bt_running && btr_host_on);
    /* Mode2 badge is independently redrawn on real connection status changes. */
    gateway_mode=GATEWAY_MODE_METERS;gateway_view_changed();app_loop();
    assert(fb[2*240+38]==((RGB(190,25,25)>>8)|(RGB(190,25,25)<<8))%65536);
    btr_host_connected=1;app_loop();assert(fb[2*240+38]==((RGB(20,150,50)>>8)|(RGB(20,150,50)<<8))%65536);
    codec_badges();
    btr_host_connected=0;app_loop();assert(!gateway_bt_seen);
    /* Save OFF stops pairing, connection and audio. */
    tap(B_EDIT);turn(EN_ALGO,-1);tap(B_SAVE);assert(!gateway_bt_mode && !btr_host_on);
    app_init();app_boot();assert(!gateway_bt_mode && !btr_host_on);
    /* Migrate all previous settings versions; invalid new mode defaults safely. */
    record=gateway_settings();record.version=3;record.reserved=record.spectrum_reserved=0;record.zoom=9;record.spectrum_max=24;record.spectrum_range=3;
    plat_store_save(OBJ_SETTINGS,&record,12);app_init();assert(gateway_bt_mode && gateway_zoom==9 && gateway_spectrum_max==24);
    record=gateway_settings();record.bluetooth=2;plat_store_save(OBJ_SETTINGS,&record,sizeof record);app_init();assert(gateway_bt_mode && gateway_zoom==6);
    mixed_audio();
    assert(!cv_cuts && !cpu_work_depth);
    puts("PASS: settings draft/save/apply/failure, persistence/migration, pairing threshold/cancel, LEDs, BT badge");
}
