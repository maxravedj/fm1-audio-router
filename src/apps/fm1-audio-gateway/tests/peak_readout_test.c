/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static uint16_t previous[240*58];
static void label(uint32_t amplitude,const char *expected)
{
    gateway_meter_t m={0};char text[8];gateway_meter_step(&m,amplitude,48);
    gateway_peak_label(text,m.peak);assert(!strcmp(text,expected));
    assert(m.level==gateway_meter_level(amplitude) && m.held==m.level);
}
static void ldac_stream_peaks(void)
{
    btr_host_on=btr_host_connected=btr_host_ldac_ready=1;btr_host_codec=PLAT_BT_CODEC_LDAC;
    gateway_screen=GATEWAY_MAIN;gateway_mode=GATEWAY_MODE_FULL;gateway_monitor=PLAT_MONITOR_BT;
    gateway_view_changed();app_loop();app_frame();
    /* Go through the actual sample producer, snapshot and UI frame, rather
     * than seeding the meter. Both channels are already below the bar floor. */
    for(unsigned cycle=0;cycle<3;cycle++){
        plat_audio_monitor_bt_begin();plat_audio_monitor_bt_sample(100,-1000);
        now_ms+=48;app_loop();app_frame();
        assert(gateway_meter[0].peak==515 && gateway_meter[1].peak==715);
        assert(gateway_peak_drawn[0]==715 && !gateway_meter[1].level);
        gateway_meter_t before[2]={gateway_meter[0],gateway_meter[1]};
        uint32_t meter_ms=gateway_meter_ms,clears=lcd_changes-blits;
        /* CONFIGURED/OPEN/START/SUSPEND all use this main-thread hook before
         * the decoder is joined/reset. A reallocation is not a source change. */
        app_bt_ldac_prepare();app_loop();app_frame();
        assert(gateway_has_wave() && !memcmp(before,gateway_meter,sizeof before));
        assert(gateway_meter_ms==meter_ms && gateway_peak_drawn[0]==715);
        assert(lcd_changes-blits==clears); /* No clear/flash on the same view. */
    }
    /* Silence after a pause follows ordinary hold/decay below -60, without
     * the LDAC workspace transition erasing the remaining numeric peak. */
    for(unsigned i=0;i<6;i++){now_ms+=200;app_loop();app_frame();}
    assert(gateway_meter[1].peak>0 && gateway_meter[1].peak<715);
    for(unsigned i=0;i<35;i++){now_ms+=200;app_loop();app_frame();}
    assert(!gateway_meter[0].peak && !gateway_meter[1].peak);
    /* When memory really changes the layout, remap by source rather than
     * treating the wide page's first (USB) pair as the selected signal. */
    const unsigned pairs[]={0,4,2,6}; /* platform source order */
    for(unsigned source=0;source<4;source++){
        gateway_monitor=source;btr_host_ldac_ready=0;gateway_view_changed();app_loop();
        assert(gateway_wide_meters());
        for(unsigned i=0;i<8;i++)gateway_meter_step(&gateway_meter[i],10*(i+1),48);
        unsigned pair=pairs[source];gateway_meter_t selected[2]={gateway_meter[pair],gateway_meter[pair+1]};
        btr_host_ldac_ready=1;app_loop();assert(gateway_has_wave());
        assert(!memcmp(selected,gateway_meter,sizeof selected));
        btr_host_ldac_ready=0;app_loop();assert(gateway_wide_meters());
        assert(!memcmp(selected,gateway_meter+pair,sizeof selected));
    }
    btr_host_ldac_ready=1;
    btr_host_connected=0;app_loop();app_frame();
    puts("PASS: actual BT producer/snapshot/frame retains quiet peaks across LDAC pause/resume workspace changes, unchanged-view redraw skips clearing, silence holds/decays to --");
}
int main(void)
{
    label(0,"--");label(8388608,"0.0");label(8389,"-60.0");
    label(10000,"-58.5");label(1000,"-78.5");label(100,"-98.5");
    label(10,"-118.5");label(1,"-138.5");
    uint32_t previous_level=0;
    for(uint32_t p=1;p<=8388608;p+=p/100+1){
        gateway_meter_t m={0};gateway_meter_step(&m,p,48);
        double db=20*log10((double)p/8388608);
        assert(fabs((double)((int)m.peak-1500)/10-db)<0.12);
        assert(m.peak>=previous_level && m.peak<=1500);previous_level=m.peak;
        assert(m.level==gateway_meter_level(p) && m.held==m.level);
    }
    gateway_meter_t m={0};gateway_meter_step(&m,100,48);assert(m.peak==515 && !m.held && !m.level);
    gateway_meter_step(&m,0,500);assert(m.peak==515);
    gateway_meter_step(&m,0,500);assert(m.peak==455 && !m.held && !m.level);
    gateway_meter_step(&m,0,5000);assert(!m.peak);
    /* Text must continue updating when the graphical bar/hold stays at zero.
     * Check both footers and every source, using the real render cache. */
    palette_set(APP_PALETTE);now_ms=100;app_init();app_ui_init();
    for(unsigned mode=GATEWAY_MODE_FULL;mode<=GATEWAY_MODE_SPECTRUM;mode++)for(unsigned source=0;source<4;source++){
        gateway_mode=mode;gateway_monitor=source;gateway_view_changed();
        gateway_meter_step(&gateway_meter[0],1,48);gateway_meter_step(&gateway_meter[1],100,48);
        gateway_draw_meter(1);assert(gateway_peak_drawn[0]==515);
        memcpy(previous,fb+182*240,sizeof previous);
        uint32_t key[2]={gateway_meter_drawn[0],gateway_meter_drawn[1]};
        unsigned start=blits;
        gateway_meter_step(&gateway_meter[0],1000,48);gateway_draw_meter(0);
        assert(gateway_peak_drawn[0]==715 && !memcmp(key,gateway_meter_drawn,sizeof key));
        assert(blits==start+1); /* One 48x12 text transfer; no bar redraw. */
        assert(memcmp(previous,fb+182*240,sizeof previous));
        memcpy(previous,fb+182*240,sizeof previous);gateway_draw_meter(1);
        assert(!memcmp(previous,fb+182*240,sizeof previous));
        unsigned before=blits;gateway_draw_meter(0);assert(blits==before);
    }
    /* Shared numeric range also reaches Home-1, Mixer and the limiter. */
    gateway_mode=GATEWAY_MODE_METERS;gateway_view_changed();
    gateway_meter_step(&gateway_meter[0],10,48);gateway_draw_meter(1);assert(gateway_peak_drawn[0]==315);
    gateway_screen=GATEWAY_MIXER;gateway_view_changed();gateway_screenshot_painting=1;
    gateway_meter_step(&gateway_meter[0],10,48);gateway_mixer_draw(1);assert((gateway_mixer_peak_key[0]&65535u)==315);
    gateway_screenshot_painting=0;gateway_screen=GATEWAY_FX;gateway_fx_limiter=1;gateway_view_changed();
    gateway_fx.output_peak[0]=10;gateway_fx_status();assert(gateway_fx_status_drawn[1]==315);
    ldac_stream_peaks();
    assert(!cv_cuts && !cpu_work_depth);
    puts("PASS: numeric peaks below -60 through a 24-bit LSB, accurate/monotonic dB conversion, unchanged bars, hold/decay/silence and live cache updates on both visualizers/all sources, Home-1, Mixer and limiter");
}
