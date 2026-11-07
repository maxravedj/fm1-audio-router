/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* USB audio around one render block (main_fm1.c render_block; tests/uac_mix_test.c), in this order:
 *   1. the USB recording (usb_audio_in: uac_tap) takes the app's render alone, so recording the FM-1 never records
 *      the computer's own playback;
 *   2. the computer's audio (usb_audio_out: uac_play) is added for the speaker and headphones, at plat_usb_play_gain's
 *      level times MASTER's (normally squared; APP_MASTER_LINEAR follows position,
 *      APP_MASTER_DB uses the -60..0 dB audio taper). uac_play runs at level 0 too: it still reads the ring, so its
 *      rate tracking stays locked and turning it up again starts in step;
 *   3. with USB AUDIO full (plat_usb_audio_full; the render came at 4096), MASTER for the speaker after the tap, over
 *      both: the computer's audio was added at plat_usb_play_gain's level alone, MASTER here;
 *   4. optional master meter tap, then APP_MASTER_FADE's user mute ramp.
 * APP_MASTER_FX instead gathers USB/BT at unity with int32 headroom, processes
 * the bus once, then applies MASTER inside app_master_process before meter/mute.
 * Recording remains app-only and both full modes use this same order, unless
 * APP_USB_OUT_ROUTING opts into app-owned source taps and recording after FX.
 * That opt-in requires silent app render, APP_INPUT_FADERS and APP_MASTER_FX;
 * its app captures a source copy while the same frames continue to playback.
 * An app with APP_USB_CAPTURE gives the recording its own channels (app_usb_capture), one with APP_USB_PLAY takes the
 * computer's audio itself (plat_usb_audio_play): step 2 is then not done here.
 * The includer has uac_tap and uac_play (bsp/chip/usb.c) when FELUCCA_UAC / FELUCCA_UAC_OUT are on. */
#ifndef FM1B_UAC_MIX_H
#define FM1B_UAC_MIX_H
#include <stdint.h>
#ifdef APP_MASTER_METER
#include "master_meter.h"
#endif
#ifdef APP_MASTER_FADE
#include "master_fade.h"
#endif

/* o: n stereo frames of the render (24-bit, as the I2S takes them; at 4096 when full, else at gain); gain: MASTER's gain
 * (APP_GAIN), put on after the tap when full; master: MASTER's position 0..4096; play_q12: the computer's audio's level,
 * 0 muted .. 4096 as sent */
static inline void uac_mix(int32_t *o, uint32_t n, uint32_t gain, uint32_t master, int full, uint32_t play_q12)
{
#if FELUCCA_UAC && !defined APP_USB_OUT_ROUTING
#ifdef APP_USB_CAPTURE
    if (uac_capturing())                            /* the USB audio input: the app's channels (0: silence) */
        uac_tap(app_usb_capture(o, n), n);
#else
    uac_tap(o, n);                                  /* the USB audio input: the app's output, before the computer's */
#endif
#endif
#if FELUCCA_UAC && defined APP_USB_OUT_ROUTING
    app_usb_out_begin(n,uac_capturing());
#endif
#if FELUCCA_UAC_OUT && !defined APP_USB_PLAY        /* (an app with APP_USB_PLAY mixes the computer's audio itself) */
#ifdef APP_MASTER_FX
    uac_play(o, n, play_q12);                       /* unity bus: MASTER follows FX */
#elif defined APP_MASTER_DB
    uac_play(o, n, full ? play_q12 : APP_GAIN(master) * play_q12 >> 12);
#elif defined APP_MASTER_LINEAR
    uac_play(o, n, full ? play_q12 : master * play_q12 >> 12);
#else
    uac_play(o, n, full ? play_q12 : (master * master >> 12) * play_q12 >> 12);   /* the computer's audio, at MASTER */
#endif
#else
    (void)master, (void)play_q12;
#endif
#ifdef APP_USB_OUT_ROUTING
    app_usb_out_usb(o,n,0);
#endif
#ifdef APP_INPUT_FADERS
    /* Opt-in gateway contract: app render is silent. USB's recording/observer
     * taps above remain raw; trim this source before adding Bluetooth. */
    app_input_fader_process(0,o,n);
#endif
#ifdef APP_USB_OUT_ROUTING
    app_usb_out_usb(o,n,1);
#endif
#ifdef APP_BT_RECEIVER
    plat_bt_receiver_mix(o, n,
#ifdef APP_MASTER_FX
        4096u);
#else
        full ? 4096u : gain);
    /* Two unity sources can exceed full scale: saturate before the I2S packer. */
    for (uint32_t i = 0; i < 2u*n; i++) {
        if (o[i] > 8388607) o[i] = 8388607;
        if (o[i] < -8388608) o[i] = -8388608;
    }
#endif
#endif
#ifdef APP_MASTER_FX
    (void)master; (void)full;
#ifdef APP_MASTER_FX_GAIN
    (void)gain;
    app_master_process(o,n,APP_MASTER_FX_GAIN(master));
#else
    app_master_process(o,n,gain);
#endif
#else
    if (full) {                                     /* the speaker and headphones: MASTER after the tap */
        const float g = (float)gain * (1.0f / 4096.0f);
        uint32_t i;
        for (i = 0; i < 2u * n; i++)
            o[i] = (int32_t)((float)o[i] * g);
    }
#endif
#if FELUCCA_UAC && defined APP_USB_OUT_ROUTING
    uac_tap(app_usb_out_end(n),n);
#endif
#ifdef APP_MASTER_METER
    master_meter_samples(o, n);                     /* read-only, after USB playback trim and MASTER */
#endif
#ifdef APP_MASTER_FADE
    master_fade_apply(o, n);                        /* user mute only: the meter does not see its fade */
#endif
}
#endif
