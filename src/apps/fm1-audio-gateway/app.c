/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Adapted from fm1-base's template for the FM-1 Audio Gateway.
 * Included by the device/host unity build, after gfx.c and announce.c. */

#include "meter.h"
#include "spectrum.h"
static uint8_t gateway_screenshot_painting,gateway_shot_frozen;
static struct {uint32_t cpu,cpu2,master;int raw;uint8_t usb,connected,pairing,codec;} gateway_shot_panel;
static void gateway_screenshot_cancel(void);
static uint8_t gateway_redraw, gateway_ui_ready, gateway_muted;
enum { GATEWAY_MAIN, GATEWAY_SETTINGS, GATEWAY_FX, GATEWAY_MIXER };
enum { GATEWAY_MODE_METERS, GATEWAY_MODE_FULL, GATEWAY_MODE_SPECTRUM, GATEWAY_MODES };
static uint8_t gateway_screen, gateway_mode, gateway_page_redraw, gateway_page_clear;
static uint8_t gateway_fx_tracking,gateway_fx_held;
static uint32_t gateway_fx_press_ms;
static void gateway_lights(void);
static void *gateway_display_block;
static uint8_t gateway_aac_active;
static uint8_t gateway_display_kind; /* 0 none, 1 waveform, 2 spectrum */
static uint8_t gateway_codec_preference, gateway_codec_draft, gateway_monitor;
static const char *const gateway_sources[]={"USB IN","BT IN","MASTER","USB OUT"};
/* Stable stored IDs; visual order is independent of their numeric values. */
static const uint8_t gateway_source_order[]={PLAT_MONITOR_USB,PLAT_MONITOR_BT,PLAT_MONITOR_OUT,PLAT_MONITOR_MASTER};
static const char *const gateway_codecs[]={"AUTO","AAC","SBC"};
enum { GATEWAY_SET_BT, GATEWAY_SET_CODEC, GATEWAY_SET_AUTO_SCALE, GATEWAY_SET_COLOR, GATEWAY_SET_SPECTRUM_COLOR, GATEWAY_SET_BATTERY, GATEWAY_SET_CPU, GATEWAY_SET_USB_OUT, GATEWAY_SET_FADE, GATEWAY_SETTING_ROWS };
enum { GATEWAY_WAVE_RED, GATEWAY_WAVE_GREEN, GATEWAY_WAVE_BLUE, GATEWAY_WAVE_YELLOW,
       GATEWAY_WAVE_GRAY, GATEWAY_WAVE_WHITE, GATEWAY_WAVE_ORANGE, GATEWAY_WAVE_COLORS };
static const char *const gateway_color_names[]={"RED","GREEN","BLUE","YELLOW","LIGHT GRAY","WHITE","ORANGE"};
static const uint16_t gateway_wave_colors[]={RGB(255,48,48),RGB(40,220,90),RGB(45,130,255),
    RGB(255,212,40),RGB(190,190,190),RGB(255,255,255),RGB(255,140,25)};
static const char *const gateway_setting_names[]={"Bluetooth:","Bluetooth codec:","Master waveform auto scale","Waveform color:","Spectrum color:","Battery display:","CPU load display:","USB OUT:","Mute/solo fade time:"};
static uint8_t gateway_master_wave_auto_scale=1, gateway_auto_scale_draft=1;
static uint8_t gateway_wave_color=GATEWAY_WAVE_BLUE, gateway_color_draft=GATEWAY_WAVE_BLUE;
static uint8_t gateway_spectrum_color=GATEWAY_WAVE_YELLOW,gateway_spectrum_color_draft=GATEWAY_WAVE_YELLOW;
static uint8_t gateway_battery_mode=1,gateway_battery_draft=1;
static uint8_t gateway_cpu_mode=1,gateway_cpu_draft=1;
static uint16_t gateway_spectrum_color_value(void){return gateway_wave_colors[gateway_spectrum_color];}
static uint8_t gateway_bt_mode, gateway_bt_draft, gateway_save_notice, gateway_bt_running;
static uint8_t gateway_pair_seen, gateway_bt_seen, gateway_codec_seen, gateway_bt_badge_redraw;
static uint8_t gateway_oct_tracking, gateway_oct_fired;
static uint32_t gateway_oct_ms;
static uint32_t gateway_bar_key=~0u;
static uint8_t gateway_settings_row,gateway_settings_first;
static void gateway_settings_draw(void);
#include "fx/master_fx.h"
#include "gain.h"
static void gateway_usb_out_publish(const fx_bank_t *b);
#include "fx/ui.h"
#include "usb_out.h"
static void gateway_settings_changed(void);
#include "mixer.h"
static int gateway_has_wave(void)
{
    return gateway_screen == GATEWAY_MAIN && gateway_mode == GATEWAY_MODE_FULL && gateway_display_kind == 1;
}
static int gateway_has_meters(void)
{
    return gateway_screen == GATEWAY_MAIN || gateway_screen==GATEWAY_MIXER;
}
static int gateway_wide_meters(void){return gateway_aac_active || gateway_mode==GATEWAY_MODE_METERS;}
static int gateway_has_spectrum(void)
{
    return gateway_screen == GATEWAY_MAIN && gateway_mode == GATEWAY_MODE_SPECTRUM && gateway_display_kind == 2;
}
static uint32_t gateway_buttons, gateway_cpu_ms, gateway_cpu_drawn;
static char gateway_message[2][24];
static gateway_meter_t gateway_meter[8]; /* USB/selected, MASTER, BT, OUT stereo pairs */
static uint32_t gateway_meter_drawn[8];
static uint32_t gateway_peak_drawn[4];
static uint8_t gateway_meter_chrome_redraw;
static uint32_t gateway_meter_ms, gateway_wave_ms, gateway_wave_head, gateway_wave_seen_ms;
#define GATEWAY_WAVE_W 228u
#define GATEWAY_WAVE_H 72u
#define GATEWAY_WAVE_TOP 34u
#define GATEWAY_WAVE_STRIDE 78u
static uint8_t gateway_wave_auto_seen;
static uint32_t gateway_wave_scale=32768u,gateway_wave_multiplier=1120u;
#define GATEWAY_WAVE_INTERVAL 16u /* restore the previous ~60 FPS refresh */
#define GATEWAY_METER_INTERVAL 48u
typedef struct {
    uint8_t plot[2][GATEWAY_WAVE_W][2], drawn[2][GATEWAY_WAVE_W][2];
    uint8_t valid[2];
    int16_t wave[GATEWAY_WAVE_W][4];
} gateway_wave_buffers_t;
static gateway_wave_buffers_t *gateway_wave_buffers;
#define gateway_plot (gateway_wave_buffers->plot)
#define gateway_plot_drawn (gateway_wave_buffers->drawn)
#define gateway_plot_valid (gateway_wave_buffers->valid)
#define gateway_wave (gateway_wave_buffers->wave)
/* Clockwise means more zoom: total visible time, from 8 seconds to 4 ms. */
static const uint16_t gateway_times[] = {8000, 4000, 2000, 1000, 512, 256, 128, 64, 32, 16, 8, 4};
static const char *const gateway_time_labels[] = {"8 s", "4 s", "2 s", "1 s", "512 ms", "256 ms", "128 ms", "64 ms", "32 ms", "16 ms", "8 ms", "4 ms"};
#define GATEWAY_ZOOMS 12
static int gateway_zoom = 6;
/* Versioned payload; the platform adds CRCs and atomic A/B commit records.
 * Keep volatile meter/history state and the physical MASTER position out of it. */
typedef struct {
    uint8_t magic[3], version, zoom, muted, mode, reserved;
    uint8_t spectrum_rate, spectrum_max, spectrum_range, spectrum_reserved; /* v13: range byte stores absolute MIN; older: span index */
    uint8_t bluetooth, bt_reserved[3];
    int16_t input_db[3];
    uint8_t mixer_controls,fade_time; /* v15: input/OUT mute bits 0..2, solo bits 3..6 */
} gateway_settings_t;
static gateway_settings_t gateway_saved;
static uint8_t gateway_settings_dirty;
static uint32_t gateway_settings_ms, gateway_settings_retry_ms;

static gateway_settings_t gateway_settings(void)
{
    gateway_settings_t s = {{'A', 'R', 'T'}, 15, (uint8_t)gateway_zoom, gateway_muted, gateway_mode, gateway_battery_mode | (!gateway_cpu_mode<<1) | (gateway_usb_out_mode<<2),
        gateway_spectrum_rate, gateway_spectrum_max, gateway_spectrum_min, gateway_spectrum_color, gateway_bt_mode,
        {gateway_codec_preference,gateway_monitor,gateway_master_wave_auto_scale | gateway_wave_color<<1},
        {gateway_mix_db[0],gateway_mix_db[1],gateway_mix_db[2]},gateway_mix_mutes | gateway_mix_solos<<3,gateway_fade_time};
    return s;
}

static void gateway_settings_changed(void)
{
    if (gateway_save_notice) { gateway_save_notice = 0;
        if (gateway_screen == GATEWAY_SETTINGS) gateway_page_redraw = 1; }
    gateway_settings_dirty = 1;
    gateway_settings_ms = plat_ms();
}

static void gateway_save_settings(uint32_t now)
{
    gateway_settings_t s;
    if (!gateway_settings_dirty || now - gateway_settings_ms < 750u ||
            now - gateway_settings_retry_ms < 5000u) return;
    s = gateway_settings();
    if (!memcmp(&s,&gateway_saved,sizeof s)) {
        gateway_settings_dirty = 0;
        return;
    }
    plat_cpu_work_begin();
    /* Main thread only; the RAM runtime keeps audio/USB/scan IRQs alive during flash writes. */
    if (plat_store_save(OBJ_SETTINGS, &s, sizeof s) == 0) {
        gateway_saved = s;
        gateway_settings_dirty = 0;
        gateway_settings_retry_ms = plat_ms() - 5000u;
    } else gateway_settings_retry_ms = plat_ms();
    plat_cpu_work_end();
}

static void gateway_lights(void)
{
    uint32_t home = 1u << B_HOME, edit = 1u << B_EDIT, fx = 1u << B_FX;
    uint32_t active = gateway_screen == GATEWAY_MAIN ? home : gateway_screen==GATEWAY_SETTINGS ? edit : gateway_screen==GATEWAY_MIXER?1u<<B_GLO:fx;
    plat_glow(0);
    plat_play_red(gateway_muted);
    uint32_t dim = (home | edit | fx) & ~active;
    if (gateway_screen == GATEWAY_SETTINGS) dim |= 1u << B_SAVE;
    if(gateway_screen==GATEWAY_FX){
        if(gateway_fx_preset_dirty(gateway_fx_limiter))dim|=1u<<B_SAVE;
        if(gateway_fx_limiter?gateway_fx_params.lim:gateway_fx_params.eq)active|=1u<<B_SEL;
        else dim|=1u<<B_SEL;
        if(gateway_fx_dialog)active|=(1u<<B_OCTDN)|(1u<<B_OCTUP);
    }
    if (gateway_bt_mode && !gateway_fx_dialog) {
        if (plat_bt_receiver_pairing()) active |= 1u << B_OCTUP;
        else dim |= 1u << B_OCTUP;
    }
    uint32_t keys=0,dim_keys=0;
    if(gateway_screen==GATEWAY_MIXER)for(unsigned g=0;g<4;g++){
        uint32_t m=1u<<gateway_mute_keys[g],s=1u<<gateway_solo_keys[g];
        if(gateway_mixer_muted(g))keys|=m;else dim_keys|=m;
        if(gateway_mix_solos&(1u<<g))keys|=s;else dim_keys|=s;
    }
    plat_lights((gateway_muted ? 1u << B_PLAY : LED_PLAY_GREEN) | active,
            keys, dim, dim_keys, 1);
    plat_lights_mid(0, 0);
}

static void gateway_release_display(void)
{
    gateway_screenshot_cancel();
    /* Detach both USB interrupt producers before any block can be reused. */
    plat_usb_input_observe(0, 0);
    plat_usb_spectrum_enable(0);
    plat_usb_wave_mem(0);
    plat_usb_spectrum_mem(0);
    gateway_display_kind = 0;
    gateway_wave_buffers = 0;
    gateway_spectrum_buffers = 0;
    gateway_twiddle_ready = 0;
    plat_work_free(gateway_display_block);
    gateway_display_block = 0;
}
static uint32_t gateway_display_retry_ms;
static void gateway_acquire_display(void)
{
    uint32_t ui_bytes = 0, capture_bytes = 0;
    if(gateway_display_block)gateway_release_display();
    gateway_display_retry_ms=plat_ms();
    if (gateway_screen != GATEWAY_MAIN || gateway_aac_active) return;
    if (gateway_mode == GATEWAY_MODE_FULL) {
        ui_bytes = (sizeof(gateway_wave_buffers_t) + 7u) & ~7u;
        capture_bytes = plat_usb_wave_mem_bytes();
    } else if (gateway_mode == GATEWAY_MODE_SPECTRUM) {
        ui_bytes = sizeof(gateway_spectrum_buffers_t);
        capture_bytes = plat_usb_spectrum_mem_bytes();
    } else return;
    gateway_display_block = plat_work_alloc(ui_bytes + capture_bytes);
    if (!gateway_display_block) return;
    if (gateway_mode == GATEWAY_MODE_FULL) {
        gateway_wave_buffers = gateway_display_block;
        memset(gateway_wave_buffers, 0, sizeof *gateway_wave_buffers);
        plat_usb_wave_mem((uint8_t *)gateway_display_block + ui_bytes);
        gateway_display_kind = 1;
    } else {
        gateway_spectrum_buffers = gateway_display_block;
        plat_usb_spectrum_mem((uint8_t *)gateway_display_block + ui_bytes);
        gateway_display_kind = 2;
    }
}

#include "profile.h"
static void gateway_view_changed(void)
{
    gateway_screenshot_cancel();
    if(gateway_screen!=GATEWAY_FX)gateway_fx_dialog=0;
    gateway_fx_save_tracking=gateway_fx_save_held=0;
#ifdef APP_UI_PROFILE
    plat_profile_stop(); /* never mix view/source lifetimes in one measurement */
#endif
    uint32_t i, now = plat_ms();
    gateway_acquire_display();
    gateway_fx_view();gateway_mixer_redraw=1;
    gateway_mixer_observe(gateway_screen==GATEWAY_MIXER);
    gateway_out_observe(gateway_screen!=GATEWAY_MIXER && gateway_has_meters() && (gateway_wide_meters() || gateway_monitor==PLAT_MONITOR_OUT));
    plat_audio_monitor_source(gateway_monitor);
    plat_usb_input_observe(gateway_has_meters() && gateway_screen!=GATEWAY_MIXER, gateway_has_wave());
    plat_master_meter_enable(gateway_has_meters());
    plat_usb_spectrum_enable(gateway_has_spectrum());
    gateway_spectrum_reset(now);
    gateway_bt_badge_redraw = 1;
    gateway_bar_key=~0u;gateway_page_clear = 1;
    gateway_page_redraw = gateway_redraw = gateway_meter_chrome_redraw = 1;
    if (gateway_has_wave()) gateway_plot_valid[0] = gateway_plot_valid[1] = 0;
    for (i = 0; i < 8u; i++) gateway_meter[i] = (gateway_meter_t){0};
    gateway_wave_ms = now - GATEWAY_WAVE_INTERVAL;
    gateway_wave_scale=32768u;gateway_wave_auto_seen=0;
    gateway_wave_seen_ms = gateway_meter_ms = now;
    gateway_wave_head = gateway_has_wave() ? plat_usb_input_wave_position() : 0;
    gateway_cpu_ms = now - 250u;
    gateway_cpu_drawn = 4294967295u;
    gateway_lights();
}

void app_bt_aac_prepare(void)
{
    if(gateway_aac_active)return;
    gateway_aac_active=1;
    gateway_view_changed();
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    (void)master_q12;
    /* Platform gathers USB+BT after this silent render, then FX -> MASTER -> meter -> mute. */
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
}

void app_panic(void)
{
#ifdef APP_UI_PROFILE
    plat_profile_stop();
#endif
    /* Main-thread updater hook: silent app output alone does not mute USB playback. */
    gateway_ui_ready = 0;
    gateway_out_observe(0);gateway_mixer_observe(0);
    gateway_release_display();
    plat_usb_input_observe(0, 0);
    plat_master_meter_enable(0);
    plat_usb_spectrum_enable(0);
    plat_usb_play_gain(0);
    plat_bt_receiver_on(0);
}

void app_splash(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 75, 240, &FONT_L, "AUDIO", RGB(45, 130, 255), 1);
    draw_text_box(0, 111, 240, &FONT_L, "ROUTER", RGB(45, 130, 255), 1);
    draw_text_box(0, 153, 240, &FONT_XS, APP_VERSION, C_WHITE, 1);
}

void app_init(void)
{
    gateway_settings_t s = {0};
    uint32_t size = plat_store_size(OBJ_SETTINGS);
    gateway_release_display();gateway_usb_out_apply(0,0);
    plat_cpu_work_reset();
    plat_cpu2_set(1);
    plat_usb_play_gain(4096u);
    gateway_message[0][0] = gateway_message[1][0] = 0;
    gateway_mix_db[0]=gateway_mix_db[1]=gateway_mix_db[2]=0;
    gateway_mix_mutes=gateway_mix_solos=0;gateway_fade_time=gateway_fade_draft=3;
    gateway_monitor = gateway_codec_preference = gateway_codec_draft = gateway_aac_active = 0;
    gateway_master_wave_auto_scale = 1;
    gateway_wave_color = GATEWAY_WAVE_BLUE;
    gateway_spectrum_color=GATEWAY_WAVE_YELLOW;gateway_battery_mode=gateway_cpu_mode=1;
    gateway_zoom = 6;
    gateway_muted = 0;
    gateway_screen = GATEWAY_MAIN;
    gateway_mode = GATEWAY_MODE_METERS;
    gateway_spectrum_rate = 1;
    gateway_spectrum_max = 0;
    gateway_spectrum_min = 90;
    gateway_bt_mode = gateway_bt_draft = gateway_save_notice = gateway_bt_running = 0;
    gateway_settings_row = gateway_settings_first = gateway_oct_tracking = gateway_oct_fired = 0;
    gateway_pair_seen = gateway_bt_seen = gateway_codec_seen = 0;
    if ((size == 8u || size == 12u || size==16u || size==20u || size==22u || size == sizeof s) &&
            plat_store_load(OBJ_SETTINGS, &s, sizeof s) == (int)size &&
            (s.version<10 || (s.input_db[0]>=(s.version>=14?GATEWAY_GAIN_OFF:-600) && s.input_db[0]<=0 && s.input_db[1]>=(s.version>=14?GATEWAY_GAIN_OFF:-600) && s.input_db[1]<=0)) &&
            (s.version<12 || (s.input_db[2]>=(s.version>=14?GATEWAY_GAIN_OFF:-600) && s.input_db[2]<=0)) &&
            (s.version<15 || (s.mixer_controls<128u && s.fade_time<6u)) &&
            s.magic[0] == 'A' && s.magic[1] == 'R' && s.magic[2] == 'T' &&
            s.zoom < GATEWAY_ZOOMS && s.muted <= 1u && (s.version>=8?s.reserved<=(s.version>=11?27u:s.version>=9?3u:1u):!s.reserved) &&
            (s.version == 1 ? s.mode == 0 : s.mode < GATEWAY_MODES) &&
            (((s.version == 1 || s.version == 2) && size == 8u) ||
             ((s.version == 3 ? size == 12u : (s.version >= 4 && s.version <= 15) && size == (s.version>=15?sizeof s:s.version>=12?22u:s.version>=10?20u:16u) &&
               s.bluetooth <= 1u && (s.version==4 ? !s.bt_reserved[0] && !s.bt_reserved[1] : s.bt_reserved[0]<=2 && s.bt_reserved[1]<=(s.version>=12?3:2)) &&
               (s.version>=6 ? s.bt_reserved[2] < (s.version>=8?2u*GATEWAY_WAVE_COLORS:12u) : !s.bt_reserved[2])) && s.spectrum_rate < 3u &&
              (s.version>=13 ? s.spectrum_max<150u && s.spectrum_range>s.spectrum_max && s.spectrum_range<=150u : s.spectrum_max<=60u && s.spectrum_range<4u) && (s.version>=8?s.spectrum_reserved<GATEWAY_WAVE_COLORS:!s.spectrum_reserved)))) {
        gateway_zoom = s.zoom;
        gateway_muted = s.muted;
        gateway_mode = s.version<7 && s.mode<2 ? 1u-s.mode : s.mode;
        if (s.version >= 3) {
            gateway_spectrum_rate = s.spectrum_rate;
            gateway_spectrum_max = s.spectrum_max;
            gateway_spectrum_min = s.version>=13?s.spectrum_range:s.spectrum_max+gateway_spectrum_legacy_ranges[s.spectrum_range];
            if (s.version >= 4) gateway_bt_mode = s.bluetooth;
            if(s.version>=5){gateway_codec_preference=s.bt_reserved[0];gateway_monitor=s.bt_reserved[1];}
            if(s.version>=6){gateway_master_wave_auto_scale=s.bt_reserved[2]&1u;gateway_wave_color=s.bt_reserved[2]>>1;}
            if(s.version>=8){gateway_battery_mode=s.reserved&1u;gateway_spectrum_color=s.spectrum_reserved;}
            if(s.version>=9)gateway_cpu_mode=!(s.reserved&2u);
            if(s.version>=11)gateway_usb_out_mode=s.reserved>>2;
            if(s.version>=10)for(unsigned i=0;i<(s.version>=12?3u:2u);i++)
                gateway_mix_db[i]=s.version<14 && s.input_db[i]==-600?GATEWAY_GAIN_OFF:s.input_db[i];
            if(s.version>=15){gateway_mix_mutes=s.mixer_controls&7u;gateway_mix_solos=s.mixer_controls>>3;gateway_fade_time=s.fade_time;}
        }
    }
    gateway_fade_draft=gateway_fade_time;
    gateway_codec_draft = gateway_codec_preference;
    gateway_auto_scale_draft = gateway_master_wave_auto_scale;
    gateway_color_draft = gateway_wave_color;
    gateway_spectrum_color_draft=gateway_spectrum_color;gateway_battery_draft=gateway_battery_mode;gateway_cpu_draft=gateway_cpu_mode;gateway_usb_out_draft=gateway_usb_out_mode;
    plat_bt_receiver_preference(gateway_codec_preference);
    gateway_bt_draft = gateway_bt_mode;
    gateway_saved = gateway_settings();
    gateway_settings_dirty = 0;
    gateway_settings_ms = plat_ms();
    gateway_settings_retry_ms = plat_ms() - 5000u;
    gateway_buttons = 0;
    gateway_keys=plat_keys();
    gateway_ui_ready = 0;
    gateway_fx_init();gateway_mixer_init();
    int32_t *record=gateway_usb_out_alloc(gateway_usb_out_mode);
    if(gateway_usb_out_mode && !record){plat_log("USB OUT: no workspace");gateway_usb_out_mode=gateway_usb_out_draft=0;}
    gateway_usb_out_apply(record,gateway_usb_out_mode);
    plat_master_mute_init(gateway_mixer_cut(3));
    gateway_view_changed();
}

void app_boot(void)
{
    if (gateway_bt_mode) gateway_bt_running = plat_bt_receiver_on(1) == 0;
}

static void gateway_settings_commit(void)
{
    gateway_settings_t s = gateway_settings();
    s.bluetooth = gateway_bt_draft;
    s.bt_reserved[0] = gateway_codec_draft;
    s.bt_reserved[2] = gateway_auto_scale_draft | gateway_color_draft<<1;
    s.spectrum_reserved=gateway_spectrum_color_draft;s.reserved=gateway_battery_draft | (!gateway_cpu_draft<<1) | (gateway_usb_out_draft<<2);
    s.fade_time=gateway_fade_draft;
    int32_t *record=gateway_usb_out_draft==gateway_usb_out_mode?gateway_usb_pcm:gateway_usb_out_alloc(gateway_usb_out_draft);
    if(gateway_usb_out_draft && !record){gateway_save_notice=2;gateway_page_redraw=1;return;}
    plat_cpu_work_begin();
    if (plat_store_save(OBJ_SETTINGS, &s, sizeof s) == 0) {
        int changed = gateway_bt_mode != gateway_bt_draft;
        gateway_codec_preference = gateway_codec_draft;
        plat_bt_receiver_preference(gateway_codec_preference);
        gateway_bt_mode = gateway_bt_draft;
        gateway_master_wave_auto_scale = gateway_auto_scale_draft;
        gateway_wave_color = gateway_color_draft;
        gateway_spectrum_color=gateway_spectrum_color_draft;gateway_battery_mode=gateway_battery_draft;gateway_cpu_mode=gateway_cpu_draft;
        gateway_usb_out_apply(record,gateway_usb_out_draft);
        gateway_fade_time=gateway_fade_draft;gateway_mixer_publish();
        gateway_bar_key=~0u;gateway_bt_badge_redraw=1;
        gateway_saved = s;
        gateway_settings_dirty = 0;
        gateway_settings_retry_ms = plat_ms() - 5000u;
        int failed = 0;
        if (changed || (gateway_bt_mode && !gateway_bt_running)) {
            failed = plat_bt_receiver_on(gateway_bt_mode) != 0;
            gateway_bt_running = gateway_bt_mode && !failed;
        }
        gateway_save_notice = failed ? 2 : 1;
        gateway_lights();
    } else {if(record!=gateway_usb_pcm)plat_work_free(record);gateway_save_notice = 2;}
    gateway_page_redraw = 1;
    plat_cpu_work_end();
}

void app_input_only(void)
{
    uint32_t pkt, i;
    int port;
    uint32_t buttons = plat_buttons();
    uint32_t keys=plat_keys(),key_pressed=gateway_ui_ready?keys&~gateway_keys:0;gateway_keys=keys;
    uint32_t pressed = gateway_ui_ready ? buttons & ~gateway_buttons : 0;
    if(gateway_shot_frozen && (buttons!=gateway_buttons || key_pressed || gateway_fx_tracking ||
            gateway_fx_save_tracking || gateway_oct_tracking))gateway_screenshot_cancel();
    if (pressed & (1u << B_PLAY)) {
        plat_cpu_work_begin();
        gateway_mixer_toggle(3,0);
        plat_cpu_work_end();
    }
    if(gateway_fx_dialog){
        gateway_oct_tracking=gateway_fx_save_tracking=0;
        if(pressed&(1u<<B_OCTDN)){gateway_fx_dialog=0;gateway_fx_notice=0;gateway_page_redraw=1;gateway_lights();}
        else if(pressed&(1u<<B_OCTUP))gateway_fx_preset_confirm();
        for(i=0;i<NE;i++)(void)plat_enc((int)i);
        gateway_buttons=buttons;return;
    }
    if (pressed & (1u << B_EDIT)) {
        if (gateway_screen != GATEWAY_SETTINGS) {
            gateway_screen = GATEWAY_SETTINGS;
            gateway_bt_draft = gateway_bt_mode;
            gateway_codec_draft = gateway_codec_preference;
            gateway_auto_scale_draft = gateway_master_wave_auto_scale;
            gateway_color_draft = gateway_wave_color;
            gateway_fade_draft=gateway_fade_time;
    gateway_spectrum_color_draft=gateway_spectrum_color;gateway_battery_draft=gateway_battery_mode;gateway_cpu_draft=gateway_cpu_mode;gateway_usb_out_draft=gateway_usb_out_mode;
            gateway_save_notice = 0;
            gateway_view_changed();
        }
    } else if(pressed & (1u<<B_GLO)){
        if(gateway_screen!=GATEWAY_MIXER){gateway_screen=GATEWAY_MIXER;gateway_save_notice=0;gateway_view_changed();}
    } else if (pressed & (1u << B_FX)) {
        gateway_fx_tracking=1;gateway_fx_held=0;gateway_fx_press_ms=plat_ms();
    } else if (pressed & (1u << B_HOME)) {
        if (gateway_screen == GATEWAY_MAIN) {
            gateway_mode = gateway_aac_active ? GATEWAY_MODE_METERS : (gateway_mode + 1u) % GATEWAY_MODES;
            gateway_settings_changed();
        } else { gateway_screen = GATEWAY_MAIN; gateway_save_notice = 0; }
        gateway_view_changed();
    }
    if(gateway_fx_tracking){
        if((buttons&(1u<<B_FX)) && !gateway_fx_held && plat_ms()-gateway_fx_press_ms>=1000u){
            fx_params_t p=gateway_fx_params;p.chain=!p.chain;gateway_fx_publish(&p);gateway_fx_held=1;
        }
        if(!(buttons&(1u<<B_FX))){
            if(!gateway_fx_held){if(gateway_screen==GATEWAY_FX)gateway_fx_limiter=!gateway_fx_limiter;
                gateway_screen=GATEWAY_FX;gateway_view_changed();}
            gateway_fx_tracking=0;
        }
    }
    if(gateway_screen==GATEWAY_FX && (pressed&(1u<<B_SEL))){
        fx_params_t p=gateway_fx_params;if(gateway_fx_limiter)p.lim=!p.lim;else p.eq=!p.eq;
        gateway_fx_publish(&p);gateway_lights();
    }
    if(gateway_screen==GATEWAY_FX){
        if(pressed&(1u<<B_SAVE)){gateway_fx_save_tracking=1;gateway_fx_save_held=0;gateway_fx_save_press_ms=plat_ms();}
        if(gateway_fx_save_tracking){
            if((buttons&(1u<<B_SAVE)) && !gateway_fx_save_held && plat_ms()-gateway_fx_save_press_ms>=1000u){
                gateway_fx_dialog=2;gateway_fx_save_held=1;
            }else if(!(buttons&(1u<<B_SAVE))){
                if(!gateway_fx_save_held)gateway_fx_dialog=1;gateway_fx_save_tracking=0;
            }
            if(gateway_fx_dialog){gateway_fx_notice=0;gateway_oct_tracking=0;gateway_page_redraw=1;gateway_lights();
                for(i=0;i<NE;i++)(void)plat_enc((int)i);gateway_buttons=buttons;return;}
        }
    }
    if (gateway_bt_mode && gateway_ui_ready) {
        if (pressed & (1u << B_OCTUP)) {
            gateway_oct_ms = plat_ms(); gateway_oct_tracking = 1; gateway_oct_fired = 0;
            if (plat_bt_receiver_pairing()) {
                plat_bt_receiver_pair(0); gateway_oct_fired = 1; gateway_lights();
            }
        }
        if (gateway_oct_tracking && (buttons & (1u << B_OCTUP)) && !gateway_oct_fired &&
                plat_ms() - gateway_oct_ms >= 1000u) {
            plat_bt_receiver_pair(1); gateway_oct_fired = 1; gateway_lights();
        }
    }
    if (!(buttons & (1u << B_OCTUP)) || !gateway_bt_mode) gateway_oct_tracking = 0;
    gateway_buttons = buttons;
    if(gateway_screen==GATEWAY_MIXER)for(unsigned g=0;g<4;g++){
        if(key_pressed&(1u<<gateway_mute_keys[g]))gateway_mixer_toggle(g,0);
        if(key_pressed&(1u<<gateway_solo_keys[g]))gateway_mixer_toggle(g,1);
    }
    /* Drain incoming musical messages without generating sound or forwarding notes.
     * The platform handles USB-MIDI update/reader commands independently. Bound a pass. */
    for (i = 0; i < 32u && plat_midi_in(&pkt, &port); i++)
        ;
    for (i = 0; i < NE; i++) {
        int delta = plat_enc((int)i);
        if(delta && gateway_shot_frozen)gateway_screenshot_cancel();
        if(gateway_screen==GATEWAY_MIXER && delta){gateway_mixer_turn(i,delta);}
        else if(gateway_screen==GATEWAY_FX && delta) {
            gateway_fx_turn(i,delta);
        } else if (gateway_screen == GATEWAY_SETTINGS && delta) {
            if (i == EN_SELECT) {
                if(delta>GATEWAY_SETTING_ROWS)delta=GATEWAY_SETTING_ROWS;
                if(delta< -GATEWAY_SETTING_ROWS)delta= -GATEWAY_SETTING_ROWS;
                int row=(int)gateway_settings_row+delta;
                if(row<0)row=0;
                if(row>=GATEWAY_SETTING_ROWS)row=GATEWAY_SETTING_ROWS-1;
                gateway_settings_row=(uint8_t)row;
                if(row<gateway_settings_first)gateway_settings_first=row;
                else if(row>=gateway_settings_first+6)gateway_settings_first=row-5;
                gateway_page_redraw=1;
            }
            else if (i == EN_ALGO) {
                uint8_t *draft=gateway_settings_row==GATEWAY_SET_BT?&gateway_bt_draft:
                    gateway_settings_row==GATEWAY_SET_CODEC?&gateway_codec_draft:
                    gateway_settings_row==GATEWAY_SET_AUTO_SCALE?&gateway_auto_scale_draft:
                    gateway_settings_row==GATEWAY_SET_COLOR?&gateway_color_draft:
                    gateway_settings_row==GATEWAY_SET_SPECTRUM_COLOR?&gateway_spectrum_color_draft:
                    gateway_settings_row==GATEWAY_SET_BATTERY?&gateway_battery_draft:
                    gateway_settings_row==GATEWAY_SET_FADE?&gateway_fade_draft:
                    gateway_settings_row==GATEWAY_SET_USB_OUT?&gateway_usb_out_draft:&gateway_cpu_draft;
                int limit=gateway_settings_row==GATEWAY_SET_FADE?5:gateway_settings_row==GATEWAY_SET_USB_OUT?6:gateway_settings_row==GATEWAY_SET_CODEC?2:
                    (gateway_settings_row==GATEWAY_SET_COLOR || gateway_settings_row==GATEWAY_SET_SPECTRUM_COLOR)?GATEWAY_WAVE_COLORS-1:1;
                if(delta>limit)delta=limit;
                if(delta< -limit)delta= -limit;
                int value=(int)*draft+delta;
                if(value<0)value=0;
                if(value>limit)value=limit;
                if(value!=*draft){*draft=(uint8_t)value;gateway_save_notice=0;gateway_page_redraw=1;}
            }
        } else if (i == EN_SELECT && delta && gateway_screen==GATEWAY_MAIN && !gateway_wide_meters()) {
            /* Reduce first so even INT_MIN/MAX turns wrap without overflow. */
            unsigned index=0;while(gateway_source_order[index]!=gateway_monitor)index++;
            int value=gateway_source_order[((int)index+delta%4+4)%4];
            if(value!=gateway_monitor){
                gateway_monitor=(uint8_t)value;
                if(gateway_wide_meters()){
                    plat_audio_monitor_source(gateway_monitor);
                    gateway_meter_chrome_redraw=1;
                }else gateway_view_changed();
                gateway_settings_changed();
            }
        } else if (i == EN_K1 && delta && gateway_has_wave()) {
            int previous = gateway_zoom;
            /* Clamp before addition, including unusually large accumulated turns. */
            if (delta > GATEWAY_ZOOMS) delta = GATEWAY_ZOOMS;
            if (delta < -GATEWAY_ZOOMS) delta = -GATEWAY_ZOOMS;
            gateway_zoom += delta;
            if (gateway_zoom < 0) gateway_zoom = 0;
            if (gateway_zoom >= GATEWAY_ZOOMS) gateway_zoom = GATEWAY_ZOOMS - 1;
            if (gateway_zoom != previous) {
                gateway_redraw = 1;
                gateway_settings_changed();
            }
        } else if (delta && gateway_has_spectrum() && (i == EN_K1 || i == EN_K2 || i == EN_K3)) {
            uint8_t *setting=i==EN_K1?&gateway_spectrum_min:i==EN_K2?&gateway_spectrum_max:&gateway_spectrum_rate;
            int value=*setting,low=i==EN_K1?gateway_spectrum_max+1:0;
            int high=i==EN_K1?150:i==EN_K2?gateway_spectrum_min-1:2;
            /* Clockwise raises displayed MIN/MAX in dB. The selected edge
             * stops before crossing the other; levels/history stay absolute. */
            if(delta>150)delta=150;if(delta< -150)delta=-150;
            value-=delta; /* Clockwise: higher dB edges, SLOW -> MID -> FAST. */
            if(value<low)value=low;if(value>high)value=high;
            if(value!=*setting){*setting=(uint8_t)value;gateway_redraw=gateway_meter_chrome_redraw=1;gateway_settings_changed();}
        }
    }
    if (gateway_screen == GATEWAY_SETTINGS && (pressed & (1u << B_SAVE))) gateway_settings_commit();
}

void app_say(const char *a, const char *b)
{
    const char *s[2] = {a, b};
    uint32_t row, i;
    for (row = 0; row < 2u; row++) {
        for (i = 0; i < 23u && s[row] && s[row][i]; i++)
            gateway_message[row][i] = s[row][i];
        gateway_message[row][i] = 0;
    }
    gateway_redraw = 1;
    ann_say(a, b, ANN_NOW);
}

static void gateway_capture_wave(uint32_t now)
{
    uint32_t head = plat_usb_input_wave_position(), idle, span;
    if (head != gateway_wave_head) {
        gateway_wave_head = head;
        gateway_wave_seen_ms = now;
    }
    idle = now - gateway_wave_seen_ms;
    /* A stopped stream scrolls off as silence. Ignore ordinary packet gaps.
     * Bound arithmetic even after days disconnected; no observer writes here. */
    idle = idle > 100u ? idle - 100u : 0;
    if (idle > 8100u) idle = 8100u;
    span = (uint32_t)gateway_times[gateway_zoom] * 441u / 10u;
    plat_usb_input_waveform(gateway_wave, GATEWAY_WAVE_W, span,
                            head + idle * 441u / 10u);
    uint32_t target=32768u;
    if(gateway_monitor==PLAT_MONITOR_MASTER && gateway_master_wave_auto_scale){
        uint32_t peak=32; /* cap display gain at 60 dB; silence remains flat */
        for(unsigned x=0;x<GATEWAY_WAVE_W;x++)for(unsigned k=0;k<4;k++){
            int32_t v=gateway_wave[x][k];uint32_t a=v<0?(uint32_t)-v:(uint32_t)v;
            if(a>peak)peak=a;
        }
        target=peak;
    }
    /* Catch larger peaks immediately; quiet changes release slowly, with a
     * 15% dead band so small peak jitter cannot rescale the entire display. */
    int automatic=gateway_monitor==PLAT_MONITOR_MASTER && gateway_master_wave_auto_scale;
    if(!automatic || !gateway_wave_auto_seen || target>gateway_wave_scale)gateway_wave_scale=target;
    else if(target*100u<gateway_wave_scale*85u)gateway_wave_scale-=(gateway_wave_scale-target+63u)/64u;
    gateway_wave_auto_seen=automatic;
    gateway_wave_multiplier=(((GATEWAY_WAVE_H-1u)/2u)<<20)/gateway_wave_scale;
}

/* Quantize once per column, then compare the actual plotted pixels. */
static uint8_t gateway_wave_y(int16_t value)
{
    int32_t half=(GATEWAY_WAVE_H-1u)/2u;
    /* Outside twice the displayed scale the result is certainly clipped.
     * Inside it, abs(product) <= 2*half*2^20: exact 32-bit arithmetic even at
     * maximum auto gain, instead of a 64-bit multiply/shift per endpoint. */
    int32_t v=value,bound=(int32_t)(2u*gateway_wave_scale);
    if(v>bound)return 0;
    if(v < -bound)return GATEWAY_WAVE_H-1u;
    int32_t product=v*(int32_t)gateway_wave_multiplier;
    int32_t y=half-(product>=0?product>>20:-((-product)>>20));
    if(y<0)y=0;
    if(y>=(int32_t)GATEWAY_WAVE_H)y=GATEWAY_WAVE_H-1;
    return (uint8_t)y;
}

typedef struct {uint32_t first,last,top,bottom;} gateway_wave_rect_t;
/* A transfer costs commands as well as pixels. Merge nearby changed runs only
 * while their bounding rectangle adds at most this small pixel-equivalent cost. */
#define GATEWAY_WAVE_MERGE_COST 24u
static uint32_t gateway_wave_rect(const gateway_wave_rect_t *r,unsigned ch,uint16_t color)
{
    uint32_t width=r->last-r->first+1u,rows=APP_CANVAS_PX/width;
    for(uint32_t band=r->top;band<=r->bottom;band+=rows) {
        uint32_t h=r->bottom-band+1u;if(h>rows)h=rows;
        cv_begin(width,h,C_BLACK);
        for(unsigned x=r->first;x<=r->last;x++)cv_rect(x-r->first,(int)gateway_plot[ch][x][0]-(int)band,1,
            gateway_plot[ch][x][1]-gateway_plot[ch][x][0]+1u,color);
        cv_blit(6u+r->first,GATEWAY_WAVE_TOP+ch*GATEWAY_WAVE_STRIDE+band);
    }
    return width*(r->bottom-r->top+1u);
}
static uint32_t gateway_wave_append(gateway_wave_rect_t *r,unsigned x,unsigned top,unsigned bottom,unsigned ch,uint16_t color)
{
    uint32_t pixels=0;
    if(r->first<GATEWAY_WAVE_W) {
        unsigned t=top<r->top?top:r->top,b=bottom>r->bottom?bottom:r->bottom;
        uint32_t old=(r->last-r->first+1u)*(r->bottom-r->top+1u);
        if((x-r->first+1u)*(b-t+1u)<=old+bottom-top+1u+GATEWAY_WAVE_MERGE_COST) {
            r->last=x;r->top=t;r->bottom=b;return 0;
        }
        pixels=gateway_wave_rect(r,ch,color);
    }
    *r=(gateway_wave_rect_t){x,x,top,bottom};return pixels;
}
/* Use the symmetric difference of the old/new filled intervals. Disjoint
 * traces erase/draw only their own runs, never the untouched gap between them.
 * Spatially bounded merges prevent auto gain magnifying a few changed pixels
 * into a full-lane transfer. The resulting framebuffer is still exact. */
static uint32_t gateway_draw_wave(int force)
{
    uint32_t pixels=0;const uint16_t color=gateway_wave_colors[gateway_wave_color];
    for(unsigned ch=0;ch<2;ch++) {
        for(unsigned x=0;x<GATEWAY_WAVE_W;x++) {
            gateway_plot[ch][x][0]=gateway_wave_y(gateway_wave[x][2*ch+1]);
            gateway_plot[ch][x][1]=gateway_wave_y(gateway_wave[x][2*ch]);
        }
        if(force || !gateway_plot_valid[ch]) {
            gateway_wave_rect_t all={0,GATEWAY_WAVE_W-1,0,GATEWAY_WAVE_H-1};
            pixels+=gateway_wave_rect(&all,ch,color);
        } else {
            gateway_wave_rect_t pending[2]={{GATEWAY_WAVE_W,0,0,0},{GATEWAY_WAVE_W,0,0,0}};
            for(unsigned x=0;x<GATEWAY_WAVE_W;x++) {
                unsigned t=gateway_plot[ch][x][0],b=gateway_plot[ch][x][1];
                unsigned ot=gateway_plot_drawn[ch][x][0],ob=gateway_plot_drawn[ch][x][1];
                if(t>ob || ot>b) {
                    pixels+=gateway_wave_append(&pending[0],x,t<ot?t:ot,t<ot?b:ob,ch,color);
                    pixels+=gateway_wave_append(&pending[1],x,t<ot?ot:t,t<ot?ob:b,ch,color);
                } else {
                    if(t!=ot)pixels+=gateway_wave_append(&pending[0],x,t<ot?t:ot,(t>ot?t:ot)-1,ch,color);
                    if(b!=ob)pixels+=gateway_wave_append(&pending[1],x,(b<ob?b:ob)+1,b>ob?b:ob,ch,color);
                }
            }
            for(unsigned i=0;i<2;i++)if(pending[i].first<GATEWAY_WAVE_W)pixels+=gateway_wave_rect(&pending[i],ch,color);
        }
        memcpy(gateway_plot_drawn[ch],gateway_plot[ch],sizeof gateway_plot[ch]);gateway_plot_valid[ch]=1;
    }
    return pixels;
}

static void gateway_cpu_label(char label[12], uint32_t value)
{
    uint32_t n = 0;
    if (value > 999u) value = 999u;
    if (value >= 100u) label[n++] = '0' + value / 100u;
    if (value >= 10u) label[n++] = '0' + value / 10u % 10u;
    label[n++] = '0' + value % 10u;
    label[n++] = '%'; label[n] = 0;
}

enum { GATEWAY_BADGE_Y=2, GATEWAY_BADGE_HEIGHT=14, GATEWAY_BADGE_GAP=2, GATEWAY_BADGE_PADDING=3 };
static void gateway_badge(unsigned x,unsigned w,const char *text,uint16_t color,int outline)
{
    cv_rect(x,GATEWAY_BADGE_Y,w,GATEWAY_BADGE_HEIGHT,color);
    if(outline)cv_rect(x+1,GATEWAY_BADGE_Y+1,w-2,GATEWAY_BADGE_HEIGHT-2,C_BLACK);
    cv_text(x+(w-text_w(&FONT_T,text))/2,3,&FONT_T,text,outline?color:C_BLACK);
}
static unsigned gateway_status_badge(unsigned x,const char *text,uint16_t color,int outline)
{
    unsigned w=text_w(&FONT_T,text)+GATEWAY_BADGE_PADDING;
    gateway_badge(x,w,text,color,outline);
    return x+w+GATEWAY_BADGE_GAP;
}
static void gateway_draw_cpu(void)
{
    char label[40],battery[40],cpu[40];gateway_cpu_drawn=gateway_shot_frozen?gateway_shot_panel.cpu:plat_cpu_work_pct();
    int raw=gateway_shot_frozen?gateway_shot_panel.raw:plat_battery_raw();
    /* Estimated level interpolated from the existing 531/561/591 ADC thresholds;
     * not a calibrated voltage measurement or coulomb counter. */
    int percent=raw>0?gateway_fx_clamp((raw-501)*100/120,0,100):-1;
    uint32_t cpu2=gateway_shot_frozen?gateway_shot_panel.cpu2:plat_cpu2_work_pct();
    int usb=gateway_shot_frozen?gateway_shot_panel.usb:plat_usb_connected();
    int connected=gateway_shot_frozen?gateway_shot_panel.connected:plat_bt_receiver_connected();
    int pairing=gateway_shot_frozen?gateway_shot_panel.pairing:plat_bt_receiver_pairing();
    unsigned codec=gateway_shot_frozen?gateway_shot_panel.codec:plat_bt_receiver_codec();
    gateway_fx_format(cpu,"CPU ",gateway_cpu_drawn,0,"/");
    unsigned cpu_len=0;while(cpu[cpu_len])cpu_len++;
    gateway_cpu_label(cpu+cpu_len,cpu2);
    if(percent<0){memcpy(battery,"BAT --",7);}else gateway_fx_format(battery,"BAT ",percent,0,"%");
    unsigned off=0;if(gateway_cpu_mode)for(unsigned i=0;cpu[i];i++)label[off++]=cpu[i];
    if(gateway_battery_mode){if(off)label[off++]=' ';for(unsigned i=0;battery[i];i++)label[off++]=battery[i];}label[off]=0;
    unsigned fx_active=gateway_fx_params.eq || gateway_fx_params.lim;
    uint32_t key=((gateway_cpu_mode?gateway_cpu_drawn:0u)<<16)|((gateway_cpu_mode?cpu2:0u)<<23)|(usb!=0)|((uint32_t)gateway_bt_mode<<1)|
        ((uint32_t)(connected!=0)<<2)|((uint32_t)(pairing!=0)<<3)|
        ((uint32_t)codec<<4)|((uint32_t)(gateway_fx_params.chain && !fx_active)<<7)|((uint32_t)gateway_fx_params.chain<<8)|((uint32_t)(percent+1)<<9)|((uint32_t)gateway_battery_mode<<31)|((uint32_t)gateway_cpu_mode<<30)|((uint32_t)(gateway_usb_out_mode!=0)<<6);
    if(key==gateway_bar_key)return;
    gateway_bar_key=key;cv_begin(240,16,C_BLACK);
    unsigned x=gateway_status_badge(3,"USB IN",usb?RGB(20,150,50):RGB(190,25,25),0);
    gateway_codec_seen=connected?codec:0;
    const char *bt=connected?(codec==PLAT_BT_CODEC_AAC?"BT IN AAC":codec==PLAT_BT_CODEC_SBC?"BT IN SBC":"BT IN"):"BT IN";
    x=gateway_status_badge(x,bt,!gateway_bt_mode?C_GRAY:pairing?RGB(190,150,15):connected?RGB(20,150,50):RGB(190,25,25),!gateway_bt_mode);
    x=gateway_status_badge(x,"FX",gateway_fx_params.chain?(fx_active?RGB(20,150,50):RGB(190,150,15)):C_GRAY,!gateway_fx_params.chain);
    gateway_status_badge(x,"USB OUT",gateway_usb_out_mode?RGB(20,150,50):C_GRAY,!gateway_usb_out_mode);
    cv_text(237-text_w(&FONT_T,label),2,&FONT_T,label,C_WHITE);cv_blit(0,0);
}
static void gateway_draw_zoom(void)
{
    /* Waveform metadata is drawn together with the held peak below the lanes. */
    gateway_meter_chrome_redraw=1;
}

static void gateway_draw(void)
{
    if (gateway_screen != GATEWAY_MAIN) return;
    gateway_draw_cpu();
    if(gateway_has_wave())gateway_draw_zoom();
    if (!gateway_aac_active && gateway_mode != GATEWAY_MODE_METERS && !gateway_display_kind) {
        cv_text(8, 100, &FONT_XS, "DISPLAY MEMORY FULL", RGB(255, 48, 48));
        cv_blit(0, 240);
    }
    if (gateway_has_wave()) {
        gateway_draw_wave(1);
    }
}

static uint16_t gateway_meter_color(uint32_t level)
{
    if (level >= 540u) return RGB(255, 48, 48);       /* -6..0 dBFS */
    if (level >= 420u) return RGB(250, 210u - (level - 420u), 35); /* yellow to orange */
    return RGB(25u + level / 6u, 180u + level / 9u, 80u - level / 20u);
}

/* One stereo peak readout: the higher held L/R peak, with the bars' own
 * 750-ms hold and subsequent decay. Values are dBFS; -- is below the floor. */
static void gateway_peak_label(char label[8], uint32_t held)
{
    uint32_t db, n = 0;
    if (!held) { label[0] = '-'; label[1] = '-'; label[2] = 0; return; }
    if (held > 600u) held = 600u;
    db = 600u - held;
    if (db) label[n++] = '-';
    if (db >= 100u) label[n++] = '0' + db / 100u;
    label[n++] = '0' + db / 10u % 10u;
    label[n++] = '.';
    label[n++] = '0' + db % 10u;
    label[n] = 0;
}

 #include "mixer_ui.h"

static void gateway_draw_meter(int force)
{
    if(!gateway_wide_meters()){
        const uint32_t peak=gateway_meter[0].held>gateway_meter[1].held?gateway_meter[0].held:gateway_meter[1].held;
        int muted=0; /* Mute/solo gray styling belongs only to Mixer. */
        uint32_t keys[2];
        for(unsigned ch=0;ch<2;ch++)keys[ch]=gateway_meter[ch].level/20u|gateway_meter[ch].held<<6|
            (gateway_meter[ch].clip_ms!=0)<<16|(uint32_t)muted<<17;
        if(!force && keys[0]==gateway_meter_drawn[0] && keys[1]==gateway_meter_drawn[1])return;
        char label[8];gateway_peak_label(label,peak);
        /* Metadata follows the selected view even while its scratch is busy. */
        unsigned footer_offset=gateway_mode==GATEWAY_MODE_SPECTRUM?14:0;
        for(unsigned band=0;band<44+footer_offset;band+=APP_CANVAS_PX/240u){
        unsigned h=44+footer_offset-band;if(h>APP_CANVAS_PX/240u)h=APP_CANVAS_PX/240u;
        cv_begin(240,h,C_BLACK);cv_oy=-(int)band;
        if(gateway_mode==GATEWAY_MODE_FULL || gateway_mode==GATEWAY_MODE_SPECTRUM){
            char source[40];unsigned n=0;const char *prefix="Source: ",*name=gateway_sources[gateway_monitor];
            while(*prefix)source[n++]=*prefix++;while(*name)source[n++]=*name++;source[n]=0;
            cv_text(6,footer_offset,&FONT_T,source,RGB(96,96,96));
            char spectrum_window[40],maximum[40];
            const char *window=gateway_time_labels[gateway_zoom];
            if(gateway_mode==GATEWAY_MODE_SPECTRUM){
                gateway_fx_format(spectrum_window,"MIN ",-(int)gateway_spectrum_min,0," MAX ");
                gateway_fx_format(maximum,"",-(int)gateway_spectrum_max,0," ");
                unsigned i=0,j=0;while(spectrum_window[i])i++;
                while(maximum[j])spectrum_window[i++]=maximum[j++];
                const char *rate=gateway_spectrum_rate_names[gateway_spectrum_rate];
                while(*rate)spectrum_window[i++]=*rate++;spectrum_window[i]=0;
                window=spectrum_window;
            }
            cv_text((240-text_w(&FONT_T,window))/2,0,&FONT_T,window,RGB(96,96,96));
            cv_text(234-text_w(&FONT_T,label),footer_offset,&FONT_T,label,muted?RGB(160,160,160):C_WHITE);
        }else cv_text((240-text_w(&FONT_T,label))/2,0,&FONT_T,label,muted?RGB(100,100,100):C_WHITE);
        static const char *scale[]={"-60","-48","-36","-24","-12","-6","0"};
        static const uint16_t levels[]={0,120,240,360,480,540,600};
        for(unsigned i=0;i<7;i++)cv_text(15+levels[i]*209/600-text_w(&FONT_T,scale[i])/2,12+footer_offset,&FONT_T,scale[i],C_GRAY);
        for(unsigned ch=0;ch<2;ch++){
            const gateway_meter_t *m=&gateway_meter[ch];
            for(unsigned s=0;s<30;s++){
                unsigned level=(s+1)*20;
                cv_rect(15+s*7,26+footer_offset+ch*10,6,7,m->level>=level?
                    (muted?RGB(90,90,90):gateway_meter_color(level)):(muted?RGB(20,20,20):RGB(21,26,26)));
            }
            if(m->held)cv_rect(15+m->held*209/600,26+footer_offset+ch*10,1,7,muted?RGB(120,120,120):C_WHITE);
            cv_rect(226,26+footer_offset+ch*10,3,7,muted?RGB(45,45,45):m->clip_ms?RGB(255,48,48):RGB(45,18,18));
            gateway_meter_drawn[ch]=keys[ch];
        }
        gateway_peak_drawn[0]=peak|(uint32_t)muted<<10;
        cv_oy=0;cv_blit(0,196-footer_offset+band);
        }
        return;
    }
    static const char *ticks[]={"0","-6","-12","-24","-36","-48","-60"};
    static const uint16_t wide_y[]={49,65,81,113,145,177,209};
    if(force)for(unsigned band=24;band<240;band+=APP_CANVAS_PX/240u){
        unsigned h=240-band;if(h>APP_CANVAS_PX/240u)h=APP_CANVAS_PX/240u;
        cv_begin(240,h,C_BLACK);
        for(unsigned t=0;t<7;t++){
            int y=wide_y[t]-6;
            cv_text(18-text_w(&FONT_T,ticks[t]),y-(int)band,&FONT_T,ticks[t],C_GRAY);
        }
        for(unsigned g=0;g<4;g++){
            unsigned source=gateway_source_order[g];
            const char *name=gateway_sources[source];
            int x=(43+54*g)-text_w(&FONT_T,name)/2;
            cv_text(x,224-(int)band,&FONT_T,name,C_GRAY);
        }
        cv_blit(0,band);
    }
    for(unsigned g=0;g<4;g++){
        unsigned pair=g==0?0:g==1?4:g==2?6:2;
        int muted=0; /* HOME retains normal meter colors, including MASTER. */
        unsigned x=26+54*g,y=44,h=165;
        unsigned segment_step=5,segment_base=160;
        unsigned peak_span=159,peak_base=164;
        for(unsigned ch=0;ch<2;ch++){
            unsigned index=pair+ch;const gateway_meter_t *m=&gateway_meter[index];
            uint32_t key=(m->level*32u/600u) | (m->held?peak_base-m->held*peak_span/600u:0u)<<6 |
                (m->clip_ms!=0)<<14 | (uint32_t)muted<<15;
            if(!force && key==gateway_meter_drawn[index])continue;
            cv_begin(16,h,C_BLACK);
            cv_rect(0,0,16,2,muted?RGB(45,45,45):m->clip_ms?RGB(255,48,48):RGB(45,18,18));
            for(unsigned segment=0;segment<32u;segment++){
                unsigned level=((segment+1)*600u+31u)/32u;
                cv_rect(0,segment_base-segment*segment_step,16,segment_step-1,
                    m->level>=level?(muted?RGB(90,90,90):gateway_meter_color(level)):muted?RGB(20,20,20):RGB(21,26,26));
            }
            if(m->held)cv_rect(0,peak_base-m->held*peak_span/600u,16,1,muted?RGB(120,120,120):C_WHITE);
            cv_blit(x+18*ch,y);gateway_meter_drawn[index]=key;
        }
        uint32_t peak=gateway_meter[pair].held>gateway_meter[pair+1].held?gateway_meter[pair].held:gateway_meter[pair+1].held;
        uint32_t key=peak|(uint32_t)muted<<10;
        if(force || key!=gateway_peak_drawn[g]){
            char label[8];gateway_peak_label(label,peak);cv_begin(34,16,C_BLACK);
            cv_text((34-text_w(&FONT_T,label))/2,4,&FONT_T,label,muted?RGB(90,90,90):C_WHITE);
            cv_blit(x,24);gateway_peak_drawn[g]=key;
        }
    }
}

void app_ui_init(void)
{
    gateway_screenshot_cancel();
    /* Also called if an update was cancelled before committing: resume playback. */
    plat_usb_play_gain(4096u);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    plat_master_mute(gateway_mixer_cut(3));
    gateway_view_changed();
    if (gateway_bt_running) plat_bt_receiver_on(1);
    if (gateway_has_wave()) gateway_capture_wave(plat_ms());
    gateway_ui_ready = 1;
}

/* Background UI timing keeps waveform work independent of the platform's UI
 * slots. A late frame is never caught up in a burst; zoom changes use the
 * same refresh interval. */
#include "screenshot.h"

void app_loop(void)
{
    uint32_t now = plat_ms();
    if (!gateway_ui_ready) return;
    gateway_screenshot_step();
#ifdef APP_UI_PROFILE
    if(plat_profile_poll())gateway_profile_restore();
#endif
    uint8_t pairing = (uint8_t)plat_bt_receiver_pairing();
    uint8_t connected = (uint8_t)plat_bt_receiver_connected();
    if (pairing != gateway_pair_seen) { gateway_pair_seen = pairing; gateway_lights(); }
    if (connected != gateway_bt_seen) { gateway_bt_seen = connected; gateway_bt_badge_redraw = 1; }
    uint8_t aac = plat_bt_receiver_codec() == PLAT_BT_CODEC_AAC;
    if (aac != gateway_aac_active) {
        gateway_aac_active = aac;
        /* Keep the saved mode intact; the AAC session temporarily uses meters. */
        gateway_view_changed();
    }
    if(!gateway_aac_active && gateway_screen==GATEWAY_MAIN && !gateway_display_block &&
       (gateway_mode==GATEWAY_MODE_FULL || gateway_mode==GATEWAY_MODE_SPECTRUM) &&
       now-gateway_display_retry_ms>=500u) {
        gateway_acquire_display();
        if(gateway_display_block)gateway_view_changed();
    }
    gateway_save_settings(now);
    gateway_fx_save(now,0);
    if(gateway_shot_frozen)return;
    if (gateway_page_redraw) {
        plat_cpu_work_begin();
        if(gateway_page_clear){lcd_fill(0,18,240,222,C_BLACK);gateway_page_clear=0;}
        if (gateway_screen == GATEWAY_MAIN) {gateway_draw_cpu();if(gateway_has_wave())gateway_draw_zoom();}
        else if(gateway_screen==GATEWAY_FX)gateway_fx_draw();
        else if(gateway_screen==GATEWAY_MIXER)gateway_mixer_draw(1);
        else gateway_settings_draw();
        gateway_page_redraw = 0;
        plat_cpu_work_end();
    }
    if(gateway_screen==GATEWAY_FX && gateway_fx_limiter && !gateway_fx_dialog && now-gateway_fx_draw_ms>=48u) {
        plat_cpu_work_begin();gateway_fx_status();plat_cpu_work_end();
    }
    if(gateway_screen==GATEWAY_MIXER && (gateway_mixer_redraw || now-gateway_meter_ms>=GATEWAY_METER_INTERVAL)){
        plat_cpu_work_begin();gateway_mixer_draw(0);plat_cpu_work_end();
    }
    /* Shared status bar on every screen; small bounded transfer at 4 Hz. */
    if(gateway_bt_badge_redraw || now-gateway_cpu_ms>=250u){
        plat_cpu_work_begin();gateway_draw_cpu();gateway_cpu_ms=now;gateway_bt_badge_redraw=0;plat_cpu_work_end();
    }
    if (gateway_has_spectrum()) {
        if (now - gateway_spectrum_ms >= GATEWAY_SPECTRUM_INTERVAL) {
            plat_cpu_work_begin();
            gateway_spectrum_draw(now, gateway_redraw);
            gateway_redraw = 0;
            plat_cpu_work_end();
        }
        return;
    }
    if (!gateway_has_wave()) return;
    if(plat_profile_flags()&PROFILE_NO_WAVE_UI)return;
    if (now - gateway_wave_ms < GATEWAY_WAVE_INTERVAL) return;
    plat_cpu_work_begin();
    gateway_wave_ms = now;
    plat_profile_span_t timing=plat_profile_begin();
    gateway_capture_wave(now);plat_profile_end(PROFILE_WAVE_READ,timing);
    if (gateway_redraw) gateway_draw_zoom();
    timing=plat_profile_begin();gateway_draw_wave(gateway_redraw);plat_profile_end(PROFILE_WAVE_DRAW,timing);
    gateway_redraw = 0;
    plat_cpu_work_end();
}

static const char *gateway_settings_value(unsigned row)
{
    if(row==GATEWAY_SET_FADE)return gateway_fade_names[gateway_fade_draft];
    if(row==GATEWAY_SET_USB_OUT)return gateway_usb_out_names[gateway_usb_out_draft];
    if(row==GATEWAY_SET_BT)return gateway_bt_draft?"RECEIVER":"OFF";
    if(row==GATEWAY_SET_CODEC)return gateway_codecs[gateway_codec_draft];
    if(row==GATEWAY_SET_AUTO_SCALE)return gateway_auto_scale_draft?"ON":"OFF";
    if(row==GATEWAY_SET_BATTERY)return gateway_battery_draft?"PERCENT":"OFF";
    if(row==GATEWAY_SET_CPU)return gateway_cpu_draft?"ON":"OFF";
    return gateway_color_names[row==GATEWAY_SET_SPECTRUM_COLOR?gateway_spectrum_color_draft:gateway_color_draft];
}

void app_frame(void)
{
    uint32_t now = plat_ms(), dt = now - gateway_meter_ms;
    plat_cpu_work_begin();
    app_input_only();
    if (ann_on() && ann_where()) {
        if(gateway_screen==GATEWAY_FX)ann_say("MASTER FX",gateway_fx_limiter?"PEAK LIMITER":gateway_fx_names[gateway_fx_row],ANN_NOW);
        else if(gateway_screen==GATEWAY_MIXER)ann_say("MIXER","USB IN KNOB ONE. BT IN KNOB TWO. USB OUT KNOB THREE. MASTER POT.",ANN_NOW);
        else if (gateway_screen == GATEWAY_SETTINGS) ann_say(gateway_setting_names[gateway_settings_row],
                gateway_settings_value(gateway_settings_row),ANN_NOW);
        else ann_say(gateway_sources[gateway_monitor], gateway_has_wave() ?
                "MONITOR. STEREO WAVEFORM. KNOB ONE ZOOM. SELECT CHANGES SOURCE." :
                gateway_has_spectrum() ? "SPECTRUM. SELECT CHANGES SOURCE. KNOB ONE MIN. TWO MAX. THREE DECAY." : "USB IN. BT IN. USB OUT. MASTER METERS.", ANN_NOW);
    }
    ann_tick(now);
    if (gateway_shot_frozen || gateway_screen != GATEWAY_MAIN || gateway_page_redraw) {
        plat_cpu_work_end();
        return;
    }
    if (now - gateway_cpu_ms >= 250u) {
        gateway_cpu_ms = now;
        gateway_draw_cpu();
    }
    if (gateway_has_meters() && (dt >= GATEWAY_METER_INTERVAL || gateway_meter_chrome_redraw)) {
        if (dt >= GATEWAY_METER_INTERVAL) {
            uint32_t peak[2], ch;
            gateway_meter_ms = now;
            if (dt > 60000u) dt = 60000u;
            if(!gateway_wide_meters() && gateway_monitor==PLAT_MONITOR_BT)plat_bt_input_peaks(peak);
            else if(!gateway_wide_meters() && gateway_monitor==PLAT_MONITOR_MASTER)plat_master_peaks(peak);
            else if(!gateway_wide_meters() && gateway_monitor==PLAT_MONITOR_OUT)gateway_out_peaks(peak);
            else plat_usb_input_peaks(peak);
            for (ch = 0; ch < 2u; ch++) gateway_meter_step(&gateway_meter[ch], peak[ch], dt);
            if(gateway_wide_meters()) {
                plat_master_peaks(peak);
                for(ch=0;ch<2;ch++)gateway_meter_step(&gateway_meter[2u+ch],peak[ch],dt);
                plat_bt_input_peaks(peak);
                for(ch=0;ch<2;ch++)gateway_meter_step(&gateway_meter[4u+ch],peak[ch],dt);
                gateway_out_peaks(peak);for(ch=0;ch<2;ch++)gateway_meter_step(&gateway_meter[6u+ch],peak[ch],dt);
            }
        }
        gateway_draw_meter(gateway_meter_chrome_redraw);
        gateway_meter_chrome_redraw = 0;
    }
    plat_cpu_work_end();
}

static void gateway_settings_draw(void)
{
    draw_text_box(6, 22, 228, &FONT_XS, "SETTINGS", C_WHITE, 0);
    unsigned first=gateway_settings_first;
    for(unsigned slot=0;slot<6;slot++) {
        unsigned row=first+slot;
        for(unsigned band=0;band<27;band+=APP_CANVAS_PX/232u){
        unsigned h=27-band;if(h>APP_CANVAS_PX/232u)h=APP_CANVAS_PX/232u;
        cv_begin(232,h,row==gateway_settings_row?RGB(18,35,55):C_BLACK);cv_oy=-(int)band;
        cv_text(6,6,&FONT_T,gateway_setting_names[row],C_WHITE);
        const char *value=gateway_settings_value(row);
        uint16_t color=C_WHITE;
        if(row==GATEWAY_SET_COLOR)color=gateway_wave_colors[gateway_color_draft];
        else if(row==GATEWAY_SET_SPECTRUM_COLOR)color=gateway_wave_colors[gateway_spectrum_color_draft];
        else if(row==GATEWAY_SET_AUTO_SCALE || row==GATEWAY_SET_CPU){
            int on=row==GATEWAY_SET_AUTO_SCALE?gateway_auto_scale_draft:gateway_cpu_draft;
            color=on?RGB(40,220,90):RGB(255,48,48);
        }else if((row==GATEWAY_SET_BT && !gateway_bt_draft) ||
                 (row==GATEWAY_SET_BATTERY && !gateway_battery_draft) ||
                 (row==GATEWAY_SET_FADE && !gateway_fade_draft) ||
                 (row==GATEWAY_SET_USB_OUT && !gateway_usb_out_draft))color=RGB(255,48,48);
        cv_text(226-text_w(&FONT_T,value),6,&FONT_T,value,color);cv_blit(0,42+slot*28+band);
        }
    }
    /* Thumb represents the six visible rows; paint in one transfer without
     * clearing the screen or covering the row selection/text. */
    unsigned track=6u*28u-1u,thumb=track*6u/GATEWAY_SETTING_ROWS;
    cv_begin(3,track,RGB(35,35,35));
    cv_rect(0,first*(track-thumb)/(GATEWAY_SETTING_ROWS-6u),3,thumb,RGB(160,160,160));cv_blit(237,42);
    draw_text_box(0,218,240,&FONT_XS,gateway_save_notice?(gateway_save_notice==1?"SAVED":"SAVE FAILED"):"",RGB(255,40,40),1);
}
