/* SPDX-License-Identifier: MPL-2.0
 * Read-only USB input peaks. The packet ISR/host render is the sole producer;
 * the main loop snapshots and clears under the platform interrupt lock. */
#ifndef FM1B_USB_INPUT_METER_H
#define FM1B_USB_INPUT_METER_H
#include "plat.h"
#include "ui_profile.h"
static volatile uint32_t usb_input_peak[2];
#ifdef APP_AUDIO_MONITOR
static volatile uint8_t audio_monitor_source;
static volatile uint32_t bt_input_peak[2];
#endif
static volatile uint8_t usb_input_meter_enabled = 1;
#ifdef APP_USB_INPUT_WAVEFORM
#include "usb_input_waveform.h"
#endif

#ifdef APP_USB_INPUT_SPECTRUM
#include "usb_input_spectrum.h"
#endif

static void usb_input_stream_start(void)
{
#ifdef APP_USB_INPUT_WAVEFORM
#ifdef APP_AUDIO_MONITOR
    if(audio_monitor_source==PLAT_MONITOR_USB)
#endif
    usb_input_wave_stream_start();
#endif
#ifdef APP_USB_INPUT_SPECTRUM
#ifdef APP_AUDIO_MONITOR
    if(audio_monitor_source==PLAT_MONITOR_USB)
#endif
    usb_input_spectrum_stream_start();
#endif
}

/* Main thread: pause hidden telemetry without touching USB audio playback.
 * Restart with empty history so time spent hidden cannot revive old traces. */
void plat_usb_input_observe(int peaks, int waveform)
{
    uint32_t lock = plat_lock();
    usb_input_meter_enabled = peaks != 0;
    usb_input_peak[0] = usb_input_peak[1] = 0;
#ifdef APP_USB_INPUT_WAVEFORM
    if (usb_input_wave_enabled != (waveform != 0)) usb_input_wave_stream_start();
    usb_input_wave_enabled = waveform != 0;
#ifdef APP_DYNAMIC_DISPLAY
    if (!usb_wave_history) usb_input_wave_enabled = 0;
#endif
#else
    (void)waveform;
#endif
    plat_unlock(lock);
}

static uint32_t usb_input_abs(int32_t v)
{
    return v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
}

static void usb_input_peak_push(uint32_t l, uint32_t r)
{
    if (l > 8388608u) l = 8388608u;
    if (r > 8388608u) r = 8388608u;
    if (l > usb_input_peak[0]) usb_input_peak[0] = l;
    if (r > usb_input_peak[1]) usb_input_peak[1] = r;
}

/* Raw USB packet, before resampling, USB trim and MASTER. No writes to PCM. */
static void usb_input_meter_packet(const void *pcm, uint32_t n, uint32_t alt)
{
    const uint8_t *p = (const uint8_t *)pcm;
    uint32_t i, l = 0, r = 0, peaks = usb_input_meter_enabled, wave = 0, spectrum = 0;
#ifdef APP_USB_INPUT_WAVEFORM
    wave = usb_input_wave_enabled;
#ifdef APP_AUDIO_MONITOR
    wave = wave && audio_monitor_source==PLAT_MONITOR_USB;
#endif
    if (wave && n) usb_input_wave_packet_begin();
#endif
#ifdef APP_USB_INPUT_SPECTRUM
    spectrum = usb_spectrum_enabled;
#ifdef APP_AUDIO_MONITOR
    spectrum = spectrum && audio_monitor_source==PLAT_MONITOR_USB;
#endif
    if (spectrum && n) usb_input_spectrum_packet_begin();
#endif
    if (!peaks && !wave && !spectrum) return;
    plat_profile_span_t timing=plat_profile_begin();
    for (i = 0; i < n; i++) {
        int32_t x, y;
        if (alt == 2u) {
            x = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8;
            y = (int32_t)((uint32_t)p[3] << 8 | (uint32_t)p[4] << 16 | (uint32_t)p[5] << 24) >> 8;
            p += 6;
        } else {
            x = (int16_t)(p[0] | (uint32_t)p[1] << 8) * 256;
            y = (int16_t)(p[2] | (uint32_t)p[3] << 8) * 256;
            p += 4;
        }
#ifdef APP_USB_INPUT_WAVEFORM
        if (wave) usb_input_wave_push(x, y);
#endif
#ifdef APP_USB_INPUT_SPECTRUM
        if (spectrum) usb_input_spectrum_push(x, y);
#endif
        if (peaks) {
            uint32_t a = usb_input_abs(x), b = usb_input_abs(y);
            if (a > l) l = a;
            if (b > r) r = b;
        }
    }
    if (peaks) usb_input_peak_push(l, r);
    plat_profile_end(PROFILE_USB,timing);
}

/* The host already holds decoded 24-bit words. */
static void usb_input_meter_samples(const int32_t *lr, uint32_t n)
{
    uint32_t i, l = 0, r = 0, peaks = usb_input_meter_enabled, wave = 0, spectrum = 0;
#ifdef APP_USB_INPUT_WAVEFORM
    wave = usb_input_wave_enabled;
#ifdef APP_AUDIO_MONITOR
    wave = wave && audio_monitor_source==PLAT_MONITOR_USB;
#endif
    if (wave && n) usb_input_wave_packet_begin();
#endif
#ifdef APP_USB_INPUT_SPECTRUM
    spectrum = usb_spectrum_enabled;
#ifdef APP_AUDIO_MONITOR
    spectrum = spectrum && audio_monitor_source==PLAT_MONITOR_USB;
#endif
    if (spectrum && n) usb_input_spectrum_packet_begin();
#endif
    if (!peaks && !wave && !spectrum) return;
    for (i = 0; i < n; i++) {
#ifdef APP_USB_INPUT_WAVEFORM
        if (wave) usb_input_wave_push(lr[2u * i], lr[2u * i + 1u]);
#endif
#ifdef APP_USB_INPUT_SPECTRUM
        if (spectrum) usb_input_spectrum_push(lr[2u * i], lr[2u * i + 1u]);
#endif
        if (peaks) {
            uint32_t a = usb_input_abs(lr[2u * i]), b = usb_input_abs(lr[2u * i + 1u]);
            if (a > l) l = a;
            if (b > r) r = b;
        }
    }
    if (peaks) usb_input_peak_push(l, r);
}

void plat_usb_input_peaks(uint32_t lr[2])
{
    uint32_t lock = plat_lock();
    lr[0] = usb_input_peak[0]; lr[1] = usb_input_peak[1];
    usb_input_peak[0] = usb_input_peak[1] = 0;
    plat_unlock(lock);
}

#ifdef APP_AUDIO_MONITOR
void plat_audio_monitor_reset(int source)
{
    uint32_t lock=plat_lock();
    if(source==PLAT_MONITOR_BT)bt_input_peak[0]=bt_input_peak[1]=0;
    if(source==audio_monitor_source){usb_input_wave_stream_start();usb_input_spectrum_stream_start();}
    plat_unlock(lock);
}
void plat_audio_monitor_source(int source)
{
    if(source<0 || source>PLAT_MONITOR_OUT)return;
    uint32_t lock=plat_lock();audio_monitor_source=(uint8_t)source;
    usb_input_wave_stream_start();usb_input_spectrum_stream_start();bt_input_peak[0]=bt_input_peak[1]=0;
    plat_unlock(lock);
}
void plat_bt_input_peaks(uint32_t lr[2])
{
    uint32_t lock=plat_lock();lr[0]=bt_input_peak[0];lr[1]=bt_input_peak[1];
    bt_input_peak[0]=bt_input_peak[1]=0;plat_unlock(lock);
}
void plat_audio_monitor_bt_begin(void)
{
    if(audio_monitor_source==PLAT_MONITOR_BT && usb_input_wave_enabled)usb_input_wave_packet_begin();
    if(audio_monitor_source==PLAT_MONITOR_BT && usb_spectrum_enabled)usb_input_spectrum_packet_begin();
}
void plat_audio_monitor_bt_sample(int32_t l,int32_t r)
{
    if(usb_input_meter_enabled){uint32_t a=usb_input_abs(l),b=usb_input_abs(r);
        if(a>8388608u)a=8388608u;
        if(b>8388608u)b=8388608u;
        if(a>bt_input_peak[0])bt_input_peak[0]=a;if(b>bt_input_peak[1])bt_input_peak[1]=b;
    }
    if(audio_monitor_source==PLAT_MONITOR_BT && (usb_input_wave_enabled || usb_spectrum_enabled)){
        if(l>8388607)l=8388607;
        if(l< -8388608)l= -8388608;
        if(r>8388607)r=8388607;
        if(r< -8388608)r= -8388608;
        if(usb_input_wave_enabled)usb_input_wave_push(l,r);
        if(usb_spectrum_enabled)usb_input_spectrum_push(l,r);
    }
}
void plat_audio_monitor_out_samples(const int32_t *lr,uint32_t n)
{
    if(audio_monitor_source!=PLAT_MONITOR_OUT || (!usb_input_wave_enabled && !usb_spectrum_enabled))return;
    plat_profile_span_t timing=plat_profile_begin();
    if(usb_input_wave_enabled)usb_input_wave_packet_begin();
    if(usb_spectrum_enabled)usb_input_spectrum_packet_begin();
    for(uint32_t i=0;i<n;i++){
        int32_t l=lr?lr[2*i]:0,r=lr?lr[2*i+1]:0;
        if(usb_input_wave_enabled)usb_input_wave_push(l,r);
        if(usb_spectrum_enabled)usb_input_spectrum_push(l,r);
    }
    plat_profile_end(PROFILE_OUT,timing);
}
static void audio_monitor_master_samples(const int32_t *lr,uint32_t n)
{
    if(audio_monitor_source!=PLAT_MONITOR_MASTER || (!usb_input_wave_enabled && !usb_spectrum_enabled))return;
    plat_profile_span_t timing=plat_profile_begin();
    if(usb_input_wave_enabled)usb_input_wave_packet_begin();
    if(usb_spectrum_enabled)usb_input_spectrum_packet_begin();
    for(uint32_t i=0;i<n;i++){
        if(usb_input_wave_enabled)usb_input_wave_push(lr[2*i],lr[2*i+1]);
        if(usb_spectrum_enabled)usb_input_spectrum_push(lr[2*i],lr[2*i+1]);
    }
    plat_profile_end(PROFILE_MASTER,timing);
}
#endif
#endif
