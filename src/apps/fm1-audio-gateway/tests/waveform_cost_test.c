/* SPDX-License-Identifier: MPL-2.0
 * Actual renderer operation counts. No target timing or CPU claim. */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
int main(void)
{
    palette_set(APP_PALETTE);app_init();app_ui_init();gateway_mode=GATEWAY_MODE_FULL;
    gateway_monitor=PLAT_MONITOR_MASTER;gateway_master_wave_auto_scale=0;
    for(unsigned zoom=0;zoom<12;zoom+=zoom==0?6:5){
        gateway_zoom=zoom;gateway_view_changed();gateway_draw_wave(1);
        uint32_t windows=0,pixels=0,max_windows=0;
        for(unsigned frame=0;frame<240;frame++){
            int32_t pcm[2*704];
            for(unsigned i=0;i<704;i++)for(unsigned ch=0;ch<2;ch++)
                pcm[2*i+ch]=(int32_t)(28000*sin((frame*704+i)*(ch?.173:.1424758573)))*256;
            master_meter_samples(pcm,704);now_ms+=16;gateway_capture_wave(now_ms);
            unsigned before=blits,n=gateway_draw_wave(0),calls=blits-before;
            if(frame>=120){pixels+=n;windows+=calls;if(calls>max_windows)max_windows=calls;}
        }
        printf("MEASURE: actual fixed-scale %u-ms stereo sine: pixels/frame %u, LCD windows/frame %u, max windows %u; no hardware timing\n",
            gateway_times[zoom],pixels/120,windows/120,max_windows);
    }
}
