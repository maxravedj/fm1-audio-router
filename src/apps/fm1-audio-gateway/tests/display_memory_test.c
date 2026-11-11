/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static void tap(int b) { held_btn=1u<<b;app_input_only();held_btn=0;app_input_only(); }
static int32_t input[512];
int main(void)
{
    palette_set(APP_PALETTE);
    uint32_t total=plat_work_available();
    now_ms=100;app_init();app_ui_init();tap(B_HOME);
    uint32_t wave=total-plat_work_available();
    assert(wave==((sizeof(gateway_wave_buffers_t)+7u)&~7u)+57344u);
    assert(usb_wave_history && !usb_spectrum_ring && gateway_wave_buffers && !gateway_spectrum_buffers);
    for(unsigned i=0;i<512;i++)input[i]=1000000;
    usb_input_meter_samples(input,256);assert(usb_wave_filled==256);
    tap(B_HOME);tap(B_HOME);assert(gateway_mode==GATEWAY_MODE_METERS && plat_work_available()==total);
    assert(!gateway_display_block && !usb_wave_history && !usb_spectrum_ring);
    /* Reuse every released byte; hidden producers must not touch it. */
    uint8_t *reuse=plat_work_alloc(total);assert(reuse);memset(reuse,0xa5,total);
    usb_input_meter_samples(input,256);
    for(unsigned i=0;i<total;i++)assert(reuse[i]==0xa5);
    assert(!plat_work_alloc(8));plat_work_free(reuse);assert(plat_work_available()==total);
    tap(B_HOME);tap(B_HOME);assert(total-plat_work_available()==73736);
    assert(!usb_wave_history && usb_spectrum_ring && !gateway_wave_buffers && gateway_spectrum_buffers);
    assert(!usb_spectrum_filled && !gateway_twiddle_ready);
    for(unsigned i=0;i<16;i++)usb_input_meter_samples(input,256);
    app_loop();assert(gateway_twiddle_ready);
    for(unsigned n=0;n<200;n++) {
        tap(B_EDIT);assert(plat_work_available()==total && !usb_wave_history && !usb_spectrum_ring);
        usb_input_meter_samples(input,256);
        tap(B_HOME);assert(gateway_has_spectrum() && total-plat_work_available()==73736 && !usb_spectrum_filled);
        tap(B_HOME);assert(plat_work_available()==total);
        tap(B_HOME);assert(gateway_has_wave() && total-plat_work_available()==wave && !usb_wave_filled);
        tap(B_HOME);assert(gateway_has_spectrum());
    }
    app_panic();assert(plat_work_available()==total && !usb_wave_history && !usb_spectrum_ring);
    app_ui_init();assert(gateway_has_spectrum());
    tap(B_EDIT);reuse=plat_work_alloc(total);assert(reuse);
    tap(B_HOME);app_loop();app_frame();assert(!gateway_display_kind && !usb_spectrum_enabled);
    plat_work_free(reuse);gateway_view_changed();assert(gateway_has_spectrum());
    plat_bt_receiver_on(1);btr_host_connected=1;btr_host_codec=PLAT_BT_CODEC_AAC;
    /* The SetConfiguration callback frees scratch synchronously, before the
     * next media packet from the same RX drain, without waiting for UI. */
    assert(gateway_display_block);app_bt_aac_prepare();
    assert(!gateway_display_block && plat_work_available()==total);
    app_loop();assert(gateway_aac_active && gateway_has_meters());
    assert(!gateway_has_spectrum() && !gateway_has_wave() && plat_work_available()==total);
    assert(!usb_wave_history && !usb_spectrum_ring && gateway_mode==GATEWAY_MODE_SPECTRUM);
    void *aac=plat_work_alloc(64000);assert(aac);
    tap(B_EDIT);tap(B_HOME);app_loop();app_frame();
    assert(!gateway_display_block && plat_work_available()==total-64000);
    plat_work_free(aac);btr_host_connected=0;app_loop();
    assert(!gateway_aac_active && gateway_has_spectrum() && total-plat_work_available()==73736);
    gateway_mode=GATEWAY_MODE_FULL;gateway_view_changed();
    btr_host_connected=1;app_loop();assert(gateway_aac_active && !gateway_display_block);
    btr_host_connected=0;app_loop();assert(gateway_has_wave() && !usb_wave_filled);
    tap(B_EDIT);
    /* Freed holes are reused, allocations do not overlap, overflow is rejected. */
    void *a=plat_work_alloc(1024),*b=plat_work_alloc(4096),*c=plat_work_alloc(1024);
    assert(a&&b&&c && a!=b && b!=c);plat_work_free(b);
    assert(plat_work_alloc(4096)==b);assert(!plat_work_alloc(UINT32_MAX));
    plat_work_free(a);plat_work_free(b);plat_work_free(c);
    assert(plat_work_available()==total && !cv_cuts && !cpu_work_depth);
    printf("PASS: display buffers released/reused; wave %u, spectrum 73736, meters/settings 0 bytes; 200 transitions, detached ISR, allocator exhaustion/fragmentation/update resume\n",wave);
}
