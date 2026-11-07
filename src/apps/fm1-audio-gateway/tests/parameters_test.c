/* SPDX-License-Identifier: MPL-2.0
 * Real app callbacks: settings survive reboot, safe defaults, LEDs and peak labels. */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static void play(void)
{
    held_btn = 1u << B_PLAY; app_input_only();
    held_btn = 0; app_input_only();
}
static void leds(int muted)
{
    uint32_t active=1u<<(gateway_screen==GATEWAY_MAIN?B_HOME:B_EDIT);
    uint32_t dim=((1u<<B_HOME)|(1u<<B_EDIT)|(1u<<B_FX)) & ~active;
    if(gateway_bt_mode)dim|=1u<<B_OCTUP;
    assert(lights_b == ((muted ? 1u << B_PLAY : LED_PLAY_GREEN)|active));
    assert(lights_db==dim && lights_glow==1 && !lit_keys && !mid_keys && !dim_keys);
    assert(lit_btn == ((1u << B_PLAY)|active) && dim_btn==dim);
}
static void defaults_and_saved_preferences(void)
{
    static const char *const defaults[]={"RECEIVER","AUTO","OFF","GREEN","YELLOW","PERCENT","ON","OFF","50ms"};
    app_init();app_boot();app_ui_init();
    assert(btr_host_on && !btr_host_pairing && !gateway_bt_seen);
    assert(master_fade_frames==2205 && !gateway_usb_pcm);
    for(unsigned row=0;row<GATEWAY_SETTING_ROWS;row++)
        assert(!strcmp(gateway_settings_value(row),defaults[row]));
    gateway_settings_t saved=gateway_settings();
    /* Previously saved preferences must win, including explicitly disabled BT. */
    saved.bluetooth=0;saved.bt_reserved[0]=2;
    saved.bt_reserved[2]=1|(GATEWAY_WAVE_BLUE<<1);
    saved.spectrum_reserved=GATEWAY_WAVE_RED;saved.reserved=2|(3<<2);
    saved.fade_time=5;
    assert(!plat_store_save(OBJ_SETTINGS,&saved,sizeof saved));
    plat_bt_receiver_on(0);unsigned writes=store_writes;
    app_init();app_boot();app_ui_init();
    gateway_settings_t actual=gateway_settings();
    assert(!memcmp(&saved,&actual,sizeof saved) && !btr_host_on && store_writes==writes);
    assert(master_fade_frames==44100 && gateway_usb_pcm);
    /* A malformed record falls back to the same fresh defaults without saving. */
    saved.version=99;assert(!plat_store_save(OBJ_SETTINGS,&saved,sizeof saved));
    app_init();app_boot();app_ui_init();
    for(unsigned row=0;row<GATEWAY_SETTING_ROWS;row++)
        assert(!strcmp(gateway_settings_value(row),defaults[row]));
    assert(master_fade_frames==2205 && !gateway_usb_pcm && btr_host_on && !btr_host_pairing);
    plat_bt_receiver_on(0);store_len[OBJ_SETTINGS]=-1;
    puts("PASS: fresh/invalid settings show requested defaults, receiver boots undiscoverable, 50ms DSP fade; valid saved preferences retained exactly without a write");
}
int main(void)
{
    palette_set(APP_PALETTE);
    defaults_and_saved_preferences();
    gateway_settings_t s;
    uint32_t writes;
    char label[8];
    now_ms = 100; app_init(); leds(0); app_ui_init(); gateway_mode=GATEWAY_MODE_FULL;gateway_view_changed();leds(0);
    assert(gateway_zoom == 6 && !gateway_muted && !gateway_settings_dirty);
    writes = store_writes;
    enc_acc[EN_K1] = 2; app_input_only();
    now_ms = 500; play(); leds(1);
    now_ms = 1249; app_loop(); assert(store_writes == writes);
    now_ms = 1250; app_loop(); assert(store_writes == writes+1);
    assert(plat_store_load(OBJ_SETTINGS,&s,sizeof s)==sizeof s && s.zoom==8 && s.muted==1);
    now_ms = 2000; app_init(); app_ui_init(); leds(1);
    assert(gateway_zoom==8 && gateway_muted && master_fade_gain==0 && master_fade_target==0);
    {int32_t lr[2]={8388607,-8388608};master_fade_apply(lr,1);assert(!lr[0] && !lr[1]);}
    play(); leds(0); now_ms=2750; app_loop(); assert(store_writes==writes+2);
    app_init(); app_ui_init(); gateway_mode=GATEWAY_MODE_FULL;gateway_view_changed();leds(0); assert(gateway_zoom==8 && !gateway_muted);
    /* Returning to the saved value needs no flash write. */
    enc_acc[EN_K1]=1;app_input_only();enc_acc[EN_K1]=-1;app_input_only();
    now_ms+=750;app_loop();assert(store_writes==writes+2 && !gateway_settings_dirty);
    /* Failed saves remain pending, are rate limited, and recover. */
    store_fail=1;enc_acc[EN_K1]=1;app_input_only();now_ms+=750;app_loop();
    {uint32_t tries=store_tries;
     assert(gateway_settings_dirty);now_ms+=4999;app_loop();assert(store_tries==tries);
     store_fail=0;now_ms++;app_loop();assert(store_tries==tries+1 && !gateway_settings_dirty);
    }
    app_init();app_ui_init();assert(gateway_zoom==9);leds(0);
    /* CRC-valid foreign or malformed records must not restore arbitrary state. */
    s=gateway_settings();s.zoom=255;assert(!plat_store_save(OBJ_SETTINGS,&s,sizeof s));
    app_init();assert(gateway_zoom==6 && !gateway_muted);leds(0);
    s=gateway_settings();s.version=99;assert(!plat_store_save(OBJ_SETTINGS,&s,sizeof s));
    app_init();assert(gateway_zoom==6 && !gateway_muted);
    s=gateway_settings();s.magic[0]='?';assert(!plat_store_save(OBJ_SETTINGS,&s,sizeof s));
    app_init();assert(gateway_zoom==6 && !gateway_muted);
    /* Debounce works across millisecond-counter rollover. */
    app_ui_init();gateway_mode=GATEWAY_MODE_FULL;gateway_view_changed();now_ms=4294967000u;enc_acc[EN_K1]=1;app_input_only();
    writes=store_writes;now_ms+=749u;app_loop();assert(store_writes==writes);
    now_ms++;app_loop();assert(store_writes==writes+1);
    /* Stereo maximum follows the existing held peaks and their decay. */
    gateway_meter[0]=(gateway_meter_t){0};gateway_meter[1]=(gateway_meter_t){0};
    gateway_meter_step(&gateway_meter[0],4194304,50); /* -6 */
    gateway_meter_step(&gateway_meter[1],2097152,50); /* -12 */
    gateway_draw_meter(1);assert(gateway_peak_drawn[0]==gateway_meter[0].held);
    gateway_meter_step(&gateway_meter[1],8388608,50);
    gateway_draw_meter(0);assert(gateway_peak_drawn[0]==600);
    gateway_meter_step(&gateway_meter[1],0,500);gateway_draw_meter(0);assert(gateway_peak_drawn[0]==600);
    gateway_meter_step(&gateway_meter[1],0,500);gateway_draw_meter(0);assert(gateway_meter[1].held==540);
    assert(gateway_peak_drawn[0]==(gateway_meter[0].held>540?gateway_meter[0].held:540));
    gateway_peak_label(label,600);assert(!strcmp(label,"0.0"));
    gateway_peak_label(label,477);assert(!strcmp(label,"-12.3"));
    gateway_peak_label(label,0);assert(!strcmp(label,"--"));
    assert(text_w(&FONT_T,"-59.9")<=34 && !cv_cuts);
    puts("PASS: settings debounce/reboot/mute-at-boot/validation/retry/wrap, PLAY colors/navigation lights, stereo peak labels/hold");
}
