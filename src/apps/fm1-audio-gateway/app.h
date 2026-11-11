/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Adapted from fm1-base's template for the FM-1 Audio Gateway. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "FM1AUDIO"
#ifndef APP_VERSION
#define APP_VERSION "DEV"
#endif
#define APP_TAGLINE "USB AUDIO GATEWAY"
#define APP_SPLASH_RGB RGB(60, 180, 235)
#define APP_PALETTE 2u
#define APP_BLOCK 256u
#define APP_CANVAS_PX (240u * 16u)
#define APP_DYNAMIC_DISPLAY
#define APP_BATTERY_RAW
#define APP_USB_STATUS
#define APP_USB_INPUT_METER
#define APP_USB_INPUT_WAVEFORM
#define APP_USB_INPUT_SPECTRUM
#define APP_MASTER_METER
#define APP_MASTER_FADE
#define APP_MASTER_DB
#define APP_CPU_WORKLOAD
#define APP_SPLASH
#define APP_BT_RECEIVER
#define APP_BLE_AT_BOOT 0
#define APP_BOOT
void app_boot(void);
#define APP_LOOP
#define APP_SCREENSHOT
#define APP_SYSEX
int app_sysex(const uint8_t *p,uint32_t n);

/* Reuse the reserved settings pair; preserve panel object ID and calibration. */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_FX, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDA000u, 0xD8000u}
#define APP_STORE_BOUNCE 1 /* Same A/B format, page buffer instead of 3840 B. */
#define APP_MASTER_FX
/* Float Q12 units retain quiet amplitudes down to silence. Other apps keep
 * the original integer gain contract; the platform samples the pot once. */
#define APP_MASTER_FX_GAIN(m) app_master_gain(m)
float app_master_gain(uint32_t position);
/* Platform calls this on the headroom USB+BT sum, before meter/mute. */
void app_master_process(int32_t *lr, uint32_t n, float gain);

void app_init(void);
void app_splash(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_loop(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

#define APP_BT_AAC
#define APP_BT_APTX
#define APP_BT_LDAC
#ifndef PLAT_HOST
#define APP_BT_SBC_CPU2
#define APP_BT_RESAMPLE_CPU2
#define APP_UI_PROFILE
#define APP_SYSEX
int app_sysex(const uint8_t *p,uint32_t n);
#endif
#include "ui_profile.h"
#define APP_BT_AAC_DISPLAY
/* Main-thread acceptance callback: release display scratch before media is
 * drained from the same radio RX queue. Does not paint or change saved mode. */
void app_bt_aac_prepare(void);
void app_bt_ldac_prepare(void);

#define APP_AUDIO_MONITOR

/* App renders silence; source trims and mute/solo gates precede the USB+BT
 * bus and MASTER FX. Raw recording/visualizer taps stay before these gates. */
#define APP_INPUT_FADERS
float app_input_fader_gain(unsigned source);
void app_input_fader_idle(unsigned source,uint32_t n);
void app_input_fader_process(unsigned source,int32_t *lr,uint32_t n);
/* Mixer-only peaks after the trim ramp, before mute/solo. BT asks for the
 * current trim after advancing app_input_fader_gain for that same frame. */
#define APP_INPUT_FADER_PRE_GATE_METER
float app_input_fader_meter_gain(unsigned source);
int app_input_fader_meter_enabled(void);
void app_input_fader_meter_peaks(unsigned source,uint32_t l,uint32_t r);

/* App-owned computer recording route; other apps retain their recording tap. */
#define APP_USB_OUT_ROUTING
void app_usb_out_begin(uint32_t n,int capturing);
void app_usb_out_usb(const int32_t *lr,uint32_t n,int post);
unsigned app_usb_out_bt_mode(void);
void app_usb_out_bt_sample(unsigned frame,unsigned ch,int32_t sample);
const int32_t *app_usb_out_end(uint32_t n);
