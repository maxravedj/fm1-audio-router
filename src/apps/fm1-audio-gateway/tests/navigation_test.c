/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void lights(void)
{
    unsigned active=1u<<(gateway_screen==GATEWAY_MAIN?B_HOME:gateway_screen==GATEWAY_FX?B_FX:B_EDIT);
    unsigned dim=(((1u<<B_HOME)|(1u<<B_EDIT)|(1u<<B_FX))&~active)|(gateway_screen==GATEWAY_SETTINGS?1u<<B_SAVE:0);
    if(gateway_screen==GATEWAY_FX){if(gateway_fx_limiter?gateway_fx_params.lim:gateway_fx_params.eq)active|=1u<<B_SEL;else dim|=1u<<B_SEL;}
    if(gateway_bt_mode)dim|=1u<<B_OCTUP;
    assert(lights_b==(active|(gateway_muted?1u<<B_PLAY:LED_PLAY_GREEN)));
    assert(dim_btn==dim && lights_db==dim && lights_glow==1 && !lit_keys && !mid_keys && !dim_keys);
}
int main(void)
{
    palette_set(APP_PALETTE);now_ms=100;app_init();app_ui_init();app_loop();app_frame();lights();plat_store_save(OBJ_SETTINGS,&gateway_saved,sizeof gateway_saved);
    assert(gateway_mode==GATEWAY_MODE_METERS && !gateway_display_block);
    /* Fresh boot: 1 meters -> 2 waveform -> 3 spectrum -> 1 meters. */
    tap(B_HOME);assert(gateway_has_wave());
    held_btn=1u<<B_HOME;app_input_only();assert(gateway_has_spectrum());
    for(unsigned i=0;i<30;i++)app_input_only();assert(gateway_has_spectrum());
    held_btn=0;app_input_only();tap(B_HOME);assert(gateway_mode==GATEWAY_MODE_METERS);
    for(unsigned mode=0;mode<3;mode++){
        assert(gateway_mode==mode);enc_acc[EN_SELECT]=1;app_input_only();tap(B_EDIT);app_loop();app_frame();lights();
        assert(gateway_screen==GATEWAY_SETTINGS && !gateway_display_block);
        assert(!usb_input_wave_enabled && !usb_input_meter_enabled && !master_meter_enabled && !usb_spectrum_enabled);
        tap(B_HOME);assert(gateway_mode==mode);lights();
        tap(B_FX);assert(gateway_screen==GATEWAY_FX && !gateway_display_block);app_loop();lights();
        tap(B_PLAY);lights();assert(gateway_muted);tap(B_PLAY);lights();assert(!gateway_muted);
        tap(B_HOME);assert(gateway_mode==mode && gateway_screen==GATEWAY_MAIN);
        now_ms+=750;app_loop();gateway_settings_t s;assert(plat_store_load(OBJ_SETTINGS,&s,sizeof s)==sizeof s && s.version==16 && s.mode==mode);
        app_init();app_ui_init();assert(gateway_mode==mode && gateway_screen==GATEWAY_MAIN);tap(B_HOME);
    }
    /* v1-v6 numeric modes translate to the same actual view, then write the current schema. */
    for(unsigned version=1;version<=6;version++)for(unsigned mode=0;mode<(version==1?1:3);mode++){
        gateway_settings_t s={{'A','R','T'},version,9,1,mode,0,1,0,3,0,0,{0,0,0}};
        unsigned size=version<3?8:version==3?12:16;
        plat_store_save(OBJ_SETTINGS,&s,size);app_init();app_ui_init();
        assert(gateway_mode==(mode<2?1-mode:mode) && gateway_zoom==9 && gateway_muted && !master_fade_gain);
    }
    gateway_settings_t s=gateway_settings();s.mode=255;plat_store_save(OBJ_SETTINGS,&s,sizeof s);app_init();app_ui_init();
    assert(gateway_mode==GATEWAY_MODE_METERS && gateway_zoom==6 && !gateway_muted);
    assert(!cv_cuts && !cpu_work_depth);
    puts("PASS: reordered HOME modes, hold/return, settings/FX release producers, LEDs/mute, current schema persistence, all v1-v6 view migrations and malformed defaults");
}
