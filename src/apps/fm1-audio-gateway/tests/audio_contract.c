/* SPDX-License-Identifier: MPL-2.0
 * Exercise the real app callbacks in the platform's host unity build. */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>

int main(void)
{
    palette_set(APP_PALETTE);
    const uint32_t sizes[] = {0, 1, 2, 127, 255, APP_BLOCK};
    const uint32_t masters[] = {0, 1, 2048, 4096};
    int32_t guarded[2 * APP_BLOCK + 2];
    unsigned s, m, i;

    app_init();
    assert(usb_play_q12 == 4096u);
    for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        for (m = 0; m < sizeof(masters) / sizeof(masters[0]); m++) {
            for (i = 0; i < sizeof(guarded) / sizeof(guarded[0]); i++)
                guarded[i] = 0x13579BDF;
            app_render(guarded + 1, sizes[s], masters[m]);
            assert(guarded[0] == 0x13579BDF);
            for (i = 1; i <= 2u * sizes[s]; i++)
                assert(guarded[i] == 0);
            for (; i < sizeof(guarded) / sizeof(guarded[0]); i++)
                assert(guarded[i] == 0x13579BDF);
        }
    }
    app_panic();
    assert(usb_play_q12 == 0u);
    app_ui_init(); gateway_mode=GATEWAY_MODE_FULL;gateway_view_changed();
    assert(usb_play_q12 == 4096u);
    puts("ok: exact stereo silence and buffer boundaries at 24 size/gain combinations; updater mute/resume");
    {
        uint32_t peak[2], value;
        gateway_meter_t meter = {0};
        static int32_t input[2 * APP_BLOCK], output[2 * APP_BLOCK];
        uint16_t left_screen[240 * 196];
        assert(gateway_meter_level(0) == 0 && gateway_meter_level(8388608) == 600);
        assert(gateway_meter_level(4194304) >= 538 && gateway_meter_level(4194304) <= 540);
        assert(gateway_meter_level(2097152) >= 478 && gateway_meter_level(2097152) <= 480);
        for (i = 0, value = 0; i <= 32768u; i++) {
            uint32_t next = gateway_meter_level(i * 256u);
            assert(next >= value && next <= 600u); value = next;
        }
        gateway_meter_step(&meter, 8388608, 50);
        assert(meter.level == 600 && meter.held == 600 && meter.clip_ms == 1000);
        gateway_meter_step(&meter, 0, 500);
        assert(meter.level == 510 && meter.held == 600 && meter.clip_ms == 500);
        gateway_meter_step(&meter, 0, 500);
        assert(meter.level == 420 && meter.held == 540 && meter.clip_ms == 0);
        gateway_meter_step(&meter, 0, 5000);
        assert(meter.level == 0 && meter.held == 0);
        for (i = 0; i < APP_BLOCK; i++) { input[2*i] = -8388608; input[2*i+1] = 4194304; }
        usb_play_buf = input; usb_play_n = APP_BLOCK; usb_play_at = 0;
        master = 0;
        sim_render(output);
        for (i = 0; i < 2 * APP_BLOCK; i++) assert(output[i] == 0);
        plat_master_peaks(peak); assert(peak[0] == 0 && peak[1] == 0);
        plat_usb_input_peaks(peak);
        assert(peak[0] == 8388608u && peak[1] == 4194304u); /* independent of MASTER */
        plat_usb_input_peaks(peak);
        assert(peak[0] == 0 && peak[1] == 0);
        gateway_draw();
        for (i = 0; i < 196; i++) memcpy(left_screen + i * 240, fb + i * 240, 240 * sizeof(uint16_t));
        gateway_meter_step(&gateway_meter[0], 8388608, 50);
        gateway_meter_step(&gateway_meter[1], 4194304, 50);
        gateway_draw_meter(1);
        for (i = 0; i < 196; i++) assert(!memcmp(left_screen + i * 240, fb + i * 240, 240 * sizeof(uint16_t)));
        assert(fb[225 * 240 + 220] == swap16(RGB(255, 48, 48)));
        assert(fb[235 * 240 + 220] == swap16(RGB(21, 26, 26)));
        for (s = 0; s < 2u; s++) for (m = 0; m < 3u; m++) {
            const uint32_t positions[] = {4096,2048,0};
            const uint32_t expected[] = {8388608,265271,0}; /* custom MASTER: midpoint -30 dB */
            uac_full = s; master = positions[m]; usb_play_at = 0;
            sim_render(output); plat_master_peaks(peak);
            assert(peak[0] == expected[m] && peak[1] == expected[m]/2u);
            assert(output[2*FX_DELAY] == -(int32_t)expected[m] && output[2*FX_DELAY+1] == (int32_t)(expected[m]/2u));
            plat_master_peaks(peak); assert(!peak[0] && !peak[1]);
            plat_usb_input_peaks(peak); assert(peak[0] == 8388608 && peak[1] == 4194304);
        }
        master = 4096; uac_full = 0; usb_play_at = 0; plat_usb_play_gain(1024);
        sim_render(output); plat_master_peaks(peak);usb_play_at=0;sim_render(output);plat_master_peaks(peak); assert(peak[0] == 2097152 && peak[1] == 1048576);
        plat_usb_play_gain(4096);
        usb_play_buf = 0;
        puts("ok: dBFS calibration/monotonicity, release/peak hold, stereo pre-MASTER levels, redraw bounds/colors");
    }
    {
        int zoom = gateway_zoom;
        enc_acc[EN_PRESET] = 100; app_input_only(); assert(gateway_zoom == zoom);
        enc_acc[EN_K1] = 2147483647; app_input_only(); assert(gateway_zoom == GATEWAY_ZOOMS - 1);
        enc_acc[EN_K1] = -2147483647; app_input_only(); assert(gateway_zoom == 0);
        enc_acc[EN_K1] = 1; app_input_only(); assert(gateway_zoom == 1);
        for (i = 0; i < NE; i++) assert(enc_acc[i] == 0);
        gateway_zoom = 0;
        gateway_capture_wave(1000);
        gateway_capture_wave(10000);
        for (i = 0; i < GATEWAY_WAVE_W; i++)
            assert(!gateway_wave[i][0] && !gateway_wave[i][1] && !gateway_wave[i][2] && !gateway_wave[i][3]);
        /* Each channel's blue full-scale trace reaches the expected lane only. */
        gateway_wave_color=GATEWAY_WAVE_BLUE;
        for (i = 0; i < GATEWAY_WAVE_W; i++) {
            gateway_wave[i][0] = gateway_wave[i][1] = 32767;
            gateway_wave[i][2] = gateway_wave[i][3] = -32768;
        }
        gateway_draw();
        assert(fb[35 * 240 + 80] == swap16(RGB(45, 130, 255)));
        assert(fb[182 * 240 + 80] == swap16(RGB(45, 130, 255)));
        assert(cv_cuts == 0);
        puts("ok: knob 1 zoom direction/clamps, stopped-input silence, separate blue L/R lanes, bounded canvases");
    }
    {
        static int32_t input[2*APP_BLOCK],output[2*APP_BLOCK];
        uint32_t peak[2];
        for(i=0;i<APP_BLOCK;i++){input[2*i]=-8388608;input[2*i+1]=4194304;}
        usb_play_buf=input;usb_play_n=APP_BLOCK;master=2048;usb_play_at=0;sim_render(output);plat_master_peaks(peak);
        held_btn=1u<<B_PLAY;app_input_only();
        assert(gateway_muted && master_fade_target==0 && (lit_btn & (1u<<B_PLAY)));
        for(i=0;i<20;i++)app_input_only();assert(gateway_muted); /* one toggle per press */
        held_btn=0;app_input_only();
        for(m=0;m<20;m++) {
            usb_play_at=0;sim_render(output);plat_master_peaks(peak);
            assert(peak[0]==265271 && peak[1]==132635); /* excludes the mute fade */
            if(m==0)assert(output[0]<0 && output[0]>-265271);
        }
        for(i=0;i<2*APP_BLOCK;i++)assert(!output[i]);
        gateway_monitor=PLAT_MONITOR_MASTER;
        gateway_meter_step(&gateway_meter[0],2097152,50);
        gateway_meter_step(&gateway_meter[1],1048576,50);
        gateway_draw_meter(1);
        assert(fb[225*240+100]==swap16(gateway_meter_color(260))); /* gray is Mixer-only */
        held_btn=1u<<B_PLAY;app_input_only();held_btn=0;app_input_only();
        assert(!gateway_muted && master_fade_target==65536 && (lights_b & LED_PLAY_GREEN));
        for(m=0;m<20;m++){usb_play_at=0;sim_render(output);}
        assert(output[0]==-265271 && output[1]==132635);
        usb_play_buf=0;
        puts("ok: PLAY edge toggle/LED, HOME meter colors retained, 100-ms fade independent of meter, -30-dB half-knob level");
    }
    return 0;
}
