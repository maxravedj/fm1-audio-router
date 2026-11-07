/* SPDX-License-Identifier: MPL-2.0
 * Exercise the real BSP packet receive path with its existing fake SIE. */
#include "plat.h"
#define APP_USB_INPUT_WAVEFORM
static uint32_t test_ms;
uint32_t plat_ms(void) { return test_ms; }
uint32_t plat_lock(void) { return 0; }
void plat_unlock(uint32_t lock) { (void)lock; }
#include "../../../device/usb_input_meter.h"
#define FELUCCA_UAC_OUT_STREAM_START() usb_input_wave_stream_start()
#define FELUCCA_UAC_OUT_OBSERVE(p, n, alt) usb_input_meter_packet((p), (n), (alt))
#define main original_bsp_test_main
#include "../../../bsp/tests/uac_test.c"
#undef main

int main(void)
{
    uint32_t peaks[2], alt, i, start;
    for (alt = 1; alt <= (TEST_O24 ? 2u : 1u); alt++) {
        usb_start();
        setup(0x00, 9, 1, 0);
        setup(0x01, 11, (uint16_t)alt, UAC_ASO_IF);
        host_alt = alt;
        for (i = 0; i < 44u; i++) host_put(i, 0, 0);
        host_put(7, -8388608, 4194304);
        host_send(44);
        start = uo_w;
        uint32_t wave_start = plat_usb_input_wave_position();
        uo_service();
        plat_usb_input_peaks(peaks);
        assert(peaks[0] == 8388608u && peaks[1] == 4194304u);
        assert(uo_w - start == 44u);         /* observer did not consume or disturb the ring */
        {
            int16_t wave[44][4];
            assert(plat_usb_input_wave_position() - wave_start == 44u);
            plat_usb_input_waveform(wave, 44, 44, plat_usb_input_wave_position());
            for (i = 0; i < 44u; i++) {
                assert(wave[i][0] == (i == 7u ? -32768 : 0));
                assert(wave[i][1] == wave[i][0]);
                assert(wave[i][2] == (i == 7u ? 16384 : 0));
                assert(wave[i][3] == wave[i][2]);
            }
        }
        plat_usb_input_peaks(peaks);
        assert(peaks[0] == 0 && peaks[1] == 0);
        for (i = 0; i < 44u; i++) host_put(i, 0, 0);
        host_send(44);
        uo_service();
        plat_usb_input_peaks(peaks);
        assert(peaks[0] == 0 && peaks[1] == 0);
        /* Re-open even before the packet-gap timeout: lifecycle hook must
         * invalidate the old stream before the first new packet. */
        setup(0x01,11,0,UAC_ASO_IF); test_ms++;
        setup(0x01,11,(uint16_t)alt,UAC_ASO_IF);
        assert(usb_wave_filled==0);
        for(i=0;i<44;i++)host_put(i,2097152,-1048576);
        host_send(44);uo_service();
        { int16_t wave[126][4];
          plat_usb_input_waveform(wave,126,352800,plat_usb_input_wave_position());
          for(i=0;i<126;i++)assert(wave[i][0]>=0 && wave[i][3]<=0);
        }
        /* Hidden history must not process packets; hidden peaks must not scan
         * PCM. USB still receives all frames through the real BSP path. */
        plat_usb_input_observe(1,0);
        start=uo_w;wave_start=usb_wave_count;
        host_send(44);uo_service();plat_usb_input_peaks(peaks);
        assert(uo_w-start==44 && usb_wave_count==wave_start && !usb_wave_filled);
        assert(peaks[0]==2097152 && peaks[1]==1048576);
        plat_usb_input_observe(0,0);
        start=uo_w;host_send(44);uo_service();plat_usb_input_peaks(peaks);
        assert(uo_w-start==44 && usb_wave_count==wave_start && !peaks[0] && !peaks[1]);
        plat_usb_input_observe(1,1);assert(!usb_wave_filled);
        host_send(44);uo_service();assert(usb_wave_count==wave_start+44);
    }
    puts("PASS: real USB packet observer: stereo peaks/history, restart, hidden observers stop, playback ring continues");
    return 0;
}
