/* SPDX-License-Identifier: MPL-2.0 */
/* Exercise the real app redraw flags and LCD transfer count, not just fills:
 * a forced meter repaint blanks/rebuilds its background with blits as well. */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static uint16_t body[240*222];
static void settled(unsigned screen,unsigned mode,int signal)
{
    gateway_screen=screen;gateway_mode=mode;gateway_view_changed();app_loop();app_frame();
    if(screen==GATEWAY_MAIN){
        for(unsigned i=0;i<8;i++)gateway_meter_step(&gateway_meter[i],signal?10000*(i+1):0,48);
        gateway_draw_meter(1);gateway_meter_chrome_redraw=0;
    }
    gateway_meter_ms=now_ms;
}
static void transition(int prepare,int ready,int connected,int codec,unsigned elapsed)
{
    gateway_meter_t saved[8];memcpy(saved,gateway_meter,sizeof saved);
    memcpy(body,fb+18*240,sizeof body);
    unsigned before=blits,clears=lcd_changes-blits;
    unsigned page=gateway_page_redraw,chrome=gateway_meter_chrome_redraw,mixer=gateway_mixer_redraw;
    btr_host_connected=connected;btr_host_codec=codec;btr_host_ldac_ready=ready;
    now_ms+=elapsed;
    if(prepare)app_bt_ldac_prepare();
    assert(gateway_page_redraw==page && gateway_meter_chrome_redraw==chrome && gateway_mixer_redraw==mixer);
    app_loop();app_frame();
    /* At most the status bar changes. Body pixels alone would miss a flash
     * that clears and restores exactly the same final meter pixels. */
    assert(blits-before<=1 && lcd_changes-blits==clears);
    assert(!memcmp(body,fb+18*240,sizeof body));
    if(!elapsed)assert(!memcmp(saved,gateway_meter,sizeof saved));
}
int main(void)
{
    palette_set(APP_PALETTE);now_ms=100;app_init();app_ui_init();
    gateway_cpu_mode=gateway_battery_mode=0;
    btr_host_on=btr_host_connected=btr_host_ldac_ready=1;btr_host_codec=PLAT_BT_CODEC_LDAC;
    app_loop();app_frame();
    for(unsigned signal=0;signal<2;signal++){
        settled(GATEWAY_MAIN,GATEWAY_MODE_METERS,signal);gateway_mixer_redraw=0;
        for(unsigned i=0;i<3;i++){
            transition(1,0,1,PLAT_BT_CODEC_LDAC,0); /* detach before decoder reset */
            transition(0,1,1,PLAT_BT_CODEC_LDAC,0); /* configured/open/start/suspend reservation */
        }
        transition(1,0,1,PLAT_BT_CODEC_NONE,0); /* delayed CLOSE after pause */
        transition(0,0,0,PLAT_BT_CODEC_NONE,0);
        transition(0,1,1,PLAT_BT_CODEC_LDAC,0);
    }
    /* After silence has settled, elapsed time must not cause a full repaint
     * when the source closes its paused stream and releases the decoder. */
    settled(GATEWAY_MAIN,GATEWAY_MODE_METERS,0);gateway_mixer_redraw=0;
    transition(1,0,1,PLAT_BT_CODEC_NONE,5000);
    transition(0,1,1,PLAT_BT_CODEC_LDAC,5000);
    /* An unavailable spectrum also visibly uses the meter page. Repeated
     * LDAC reservations/failed allocations must leave that page untouched. */
    void *pressure=plat_work_alloc_top(plat_work_available()-70000);assert(pressure);
    settled(GATEWAY_MAIN,GATEWAY_MODE_SPECTRUM,1);gateway_mixer_redraw=0;
    assert(gateway_wide_meters() && !gateway_display_block);
    for(unsigned i=0;i<3;i++){
        transition(1,0,1,PLAT_BT_CODEC_LDAC,0);
        transition(0,1,1,PLAT_BT_CODEC_LDAC,0);
    }
    gateway_mode=GATEWAY_MODE_METERS;gateway_view_changed();plat_work_free(pressure);
    /* Screens with no capture buffers have no decoder-dependent body either. */
    for(unsigned screen=GATEWAY_SETTINGS;screen<=GATEWAY_MIXER;screen++){
        settled(screen,GATEWAY_MODE_METERS,0);gateway_mixer_redraw=0;
        transition(1,0,1,PLAT_BT_CODEC_LDAC,0);
        transition(0,1,1,PLAT_BT_CODEC_LDAC,0);
    }
    assert(!cv_cuts && !cpu_work_depth);
    puts("PASS: LDAC prepare/reserve/start/pause/delayed close/disconnect updates only status on unchanged Home meters, including spectrum fallback and silent elapsed time; no body blits/fills or peak reset; nongraph screens remain intact");
}
