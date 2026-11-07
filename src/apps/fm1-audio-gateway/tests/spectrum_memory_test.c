/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static void view(unsigned mode){gateway_mode=mode;gateway_view_changed();}
int main(void)
{
    palette_set(APP_PALETTE);app_init();app_ui_init();view(GATEWAY_MODE_METERS);
    /* Restrict the host arena below .44's 113584-byte guarded target budget. */
    void *reserved=plat_work_alloc_top(plat_work_available()-110592);assert(reserved);
    void *record=plat_work_alloc(gateway_usb_out_bytes(GATEWAY_USB_BT_FX));assert(record);
    view(GATEWAY_MODE_FULL);assert(gateway_has_wave());
    void *old_queue=plat_work_alloc(4264);assert(old_queue);
    view(GATEWAY_MODE_SPECTRUM);assert(!gateway_display_block && !usb_spectrum_enabled);
    now_ms+=500;app_loop();assert(!gateway_display_block && usb_input_meter_enabled); /* failed retry stays bounded */
    plat_work_free(old_queue);now_ms+=500;app_loop();
    assert(gateway_has_spectrum() && usb_spectrum_enabled && gateway_display_block);
    view(GATEWAY_MODE_FULL);assert(gateway_has_wave());
    void *queue=plat_work_alloc_top(4264);assert(queue);
    for(unsigned i=0;i<50;i++) {
        view(GATEWAY_MODE_SPECTRUM);assert(gateway_has_spectrum() && usb_spectrum_enabled);
        view(GATEWAY_MODE_FULL);assert(gateway_has_wave());
    }
    view(GATEWAY_MODE_SPECTRUM);plat_work_free(queue);now_ms+=500;app_loop();assert(gateway_has_spectrum());
    view(GATEWAY_MODE_METERS);plat_work_free(record);plat_work_free(reserved);
    assert(plat_work_available()==sizeof work_host_storage);
    /* Top allocator: slot exhaustion, gap reuse, alignment, overflow and nonoverlap. */
    void *blocks[8];for(unsigned i=0;i<8;i++){blocks[i]=plat_work_alloc_top(9+i);assert(blocks[i] && !((uintptr_t)blocks[i]&7));}
    assert(!plat_work_alloc_top(8) && !plat_work_alloc_top(UINT32_MAX) && !plat_work_alloc_top(0));
    void *gap=blocks[3];plat_work_free(gap);blocks[3]=plat_work_alloc_top(9);assert(blocks[3]==gap);
    for(unsigned i=0;i<8;i++)plat_work_free(blocks[i]);
    assert(plat_work_available()==sizeof work_host_storage && !cv_cuts);
    puts("PASS: reproduce SBC allocation fragmentation; automatic spectrum recovery after release, high-end codec scratch survives 100 waveform/FFT swaps with maximum recording, allocator bounds/reuse");
}
