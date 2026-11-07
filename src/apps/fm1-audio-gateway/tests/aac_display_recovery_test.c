/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static void tick(void){now_ms+=500;app_loop();app_frame();}
static uint16_t footer[240*58];
static void footer_matches(void)
{
    gateway_meter[0]=(gateway_meter_t){.level=400,.held=520};
    gateway_meter[1]=(gateway_meter_t){.level=300,.held=500};
    gateway_draw_meter(1);memcpy(footer,fb+182*240,sizeof footer);
    gateway_release_display();gateway_draw_meter(1);
    assert(!memcmp(footer,fb+182*240,sizeof footer));
}
int main(void)
{
    palette_set(APP_PALETTE);app_init();app_ui_init();
    /* Restrict the arena below .56's 110272-byte guarded target budget. */
    void *reserved=plat_work_alloc_top(plat_work_available()-109824);assert(reserved);
    for(unsigned mode=GATEWAY_MODE_FULL;mode<=GATEWAY_MODE_SPECTRUM;mode++)
        for(unsigned source=0;source<4;source++){
            gateway_mode=mode;gateway_monitor=source;gateway_view_changed();
            assert(gateway_display_block);footer_matches();
        }
    plat_bt_receiver_on(1);
    for(unsigned mode=0;mode<=GATEWAY_USB_MASTER_FX;mode++)for(unsigned cycle=0;cycle<10;cycle++){
        gateway_usb_out_apply(0,0);
        gateway_mode=GATEWAY_MODE_SPECTRUM;gateway_view_changed();assert(gateway_has_spectrum());
        btr_host_connected=1;btr_host_codec=PLAT_BT_CODEC_AAC;
        app_bt_aac_prepare();app_loop();assert(gateway_aac_active && !gateway_display_block);
        /* Exact target allocation sizes. The app host intentionally doesn't
         * link AAC; decoder protocol/arithmetic are outside this UI test. */
        void *aac=plat_work_alloc(31512),*decoder=plat_work_alloc(48768);assert(aac && decoder);
        gateway_usb_out_apply(gateway_usb_out_alloc(mode),mode);
        assert(!mode || gateway_usb_pcm);
        if(cycle==0){
            /* If disconnect is noticed before scratch becomes available,
             * keep metadata intact and retry without leaving the view. */
            btr_host_connected=0;tick();assert(!gateway_aac_active && !gateway_has_spectrum());
            tick();assert(!gateway_has_spectrum() && usb_input_meter_enabled);
        }
        plat_work_free(decoder);plat_work_free(aac);btr_host_connected=0;tick();
        assert(!gateway_aac_active && gateway_has_spectrum() && usb_spectrum_enabled);
        assert(!usb_wave_history && usb_spectrum_filled==0);
        /* SBC's high-end queue and the maximum route still fit after AAC. */
        void *sbc=plat_work_alloc_top(4256);assert(sbc);
        gateway_mode=GATEWAY_MODE_FULL;gateway_view_changed();assert(gateway_has_wave());
        gateway_mode=GATEWAY_MODE_SPECTRUM;gateway_view_changed();assert(gateway_has_spectrum());
        plat_work_free(sbc);
        /* Fresh selected-source input reaches the actual FFT after recovery. */
        gateway_monitor=PLAT_MONITOR_USB;gateway_view_changed();
        for(unsigned i=0;i<PLAT_SPECTRUM_FRAMES;i++){
            int32_t v=(int32_t)(4194304.0*sin(6.283185307179586*1000*i/44100.0));
            int32_t lr[]={v,-v};usb_input_meter_samples(lr,1);
        }
        now_ms+=33;app_loop();assert(gateway_spectrum_peak[22]>-10000);
        assert(gateway_spectrum_min==90 && gateway_spectrum_max==0 && gateway_spectrum_rate==1);
    }
    gateway_usb_out_apply(0,0);gateway_mode=GATEWAY_MODE_METERS;gateway_view_changed();
    plat_work_free(reserved);assert(plat_work_available()==sizeof work_host_storage);
    assert(!cv_cuts && !cpu_work_depth);
    puts("PASS: AAC disconnect restores spectrum in 70 target-sized SRAM cycles with all USB routes; fresh FFT, SBC capacity, delayed-free retry and no leaked scratch");
    puts("PASS: pending waveform/spectrum retains identical source/settings/right-aligned peak footer for all four sources");
}
