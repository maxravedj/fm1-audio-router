/* SPDX-License-Identifier: MPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#define APP_UI_PROFILE
#define APP_SYSEX
#include <stdint.h>
static uint32_t profile_icfg=0x200u;
static uint32_t fm1_icfg(void){return profile_icfg;}
static void fm1_irq_off(void){profile_icfg&=~0x200u;}
static void fm1_irq_on(void){profile_icfg|=0x200u;}
static uint32_t irq_save(void){return 0;}
static void irq_restore(uint32_t f){(void)f;}
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include "../../../device/ui_profile.c"
int main(void)
{
    palette_set(APP_PALETTE);app_init();app_ui_init();
    gateway_mode=GATEWAY_MODE_FULL;gateway_monitor=PLAT_MONITOR_MASTER;gateway_master_wave_auto_scale=0;
    gateway_view_changed();app_loop();app_frame();gateway_settings_t settings=gateway_settings();
    uint8_t command[]={0x7d,0x4c,0x50,1,1,PROFILE_NO_WAVE_UI|PROFILE_NO_WAVE_CAPTURE|PROFILE_IDLE_SLOW};
    assert(app_sysex(command,sizeof command) && plat_profile_info(0));
    uint32_t before=usb_wave_count;int32_t pcm[512];for(unsigned i=0;i<512;i++)pcm[i]=123*256;
    fm1_irq_off();master_meter_samples(pcm,256);assert(!(profile_icfg&0x200u));fm1_irq_on();
    assert(usb_wave_count==before);
    unsigned b=blits;gateway_wave_ms=now_ms-16;gateway_wave_head=123;
    app_loop();assert(gateway_wave_head==123); /* foreground capture is skipped */
    now_ms+=1001;ui_profile_start-=1001u*24000u;app_loop();assert(!plat_profile_flags() && ui_profile_idle_spins==64);
    assert(blits>b && !usb_wave_filled); /* fresh history and full restore */
    master_meter_samples(pcm,256);assert(usb_wave_count==before+256);
    gateway_settings_t after=gateway_settings();assert(!memcmp(&settings,&after,sizeof settings));
    command[5]=PROFILE_NO_WAVE_UI;assert(app_sysex(command,sizeof command));
    fm1_irq_off();master_meter_samples(pcm,256);assert(!(profile_icfg&0x200u));fm1_irq_on();
    assert(usb_wave_count==before+512); /* producer-only */
    gateway_mode=GATEWAY_MODE_METERS;gateway_view_changed();assert(!plat_profile_info(0));
    assert(app_sysex(command,sizeof command) && !plat_profile_info(0)); /* invalid view */
    command[5]=0;assert(app_sysex(command,sizeof command) && plat_profile_info(0));
    app_panic();assert(!plat_profile_info(0) && !plat_profile_flags());
    puts("PASS: actual app profiling protocol, producer/UI isolation, expiry redraw/fresh history, view/updater cleanup and unchanged saved parameters");
}
