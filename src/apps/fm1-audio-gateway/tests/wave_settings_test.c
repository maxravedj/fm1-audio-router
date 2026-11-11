/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static void tap(int b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void turn(int e,int n){enc_acc[e]=n;app_input_only();}
static void row(unsigned r){turn(EN_SELECT,INT_MIN);turn(EN_SELECT,r);assert(gateway_settings_row==r);}
static void settings(void)
{
    assert(!gateway_master_wave_auto_scale && gateway_wave_color==1);
    tap(B_EDIT);row(2);turn(EN_ALGO,1);
    assert(gateway_auto_scale_draft && !gateway_master_wave_auto_scale);
    row(3);turn(EN_ALGO,4);assert(gateway_color_draft==5 && gateway_wave_color==1);
    unsigned writes=store_writes;now_ms+=1000;app_loop();assert(store_writes==writes);
    store_fail=1;tap(B_SAVE);assert(gateway_save_notice==2 && gateway_wave_color==1 && !gateway_master_wave_auto_scale);
    store_fail=0;tap(B_SAVE);assert(gateway_save_notice==1 && gateway_wave_color==5 && gateway_master_wave_auto_scale);
    app_loop();unsigned blit=blits;now_ms+=1000;app_loop();assert(blits<=blit+1 && gateway_save_notice==1);
    turn(EN_ALGO,-1);assert(!gateway_save_notice && gateway_wave_color==5 && gateway_color_draft==4);
    tap(B_HOME);tap(B_EDIT);assert(gateway_color_draft==5 && gateway_auto_scale_draft);
    app_init();app_ui_init();assert(gateway_wave_color==5 && gateway_master_wave_auto_scale);
    /* All combinations persist atomically with codec/monitor in 16 bytes. */
    for(unsigned scale=0;scale<2;scale++)for(unsigned color=0;color<7;color++){
        tap(B_EDIT);row(2);turn(EN_ALGO,scale?INT_MAX:INT_MIN);
        row(3);turn(EN_ALGO,INT_MIN);turn(EN_ALGO,color);tap(B_SAVE);
        gateway_settings_t s;assert(plat_store_load(OBJ_SETTINGS,&s,sizeof s)==sizeof s && s.version==17);
        assert(s.bt_reserved[2]==(scale | color<<1));app_init();app_ui_init();
        assert(gateway_master_wave_auto_scale==scale && gateway_wave_color==color);
    }
    puts("PASS: four settings rows, draft-only edits, failed SAVE atomicity, discard, red SAVED lifetime, all 14 auto-scale/color combinations persist and restore");
}
static void migration(void)
{
    for(unsigned version=1;version<6;version++){
        gateway_settings_t s=gateway_settings();s.version=version;s.bt_reserved[0]=0;s.spectrum_range=3;s.zoom=9;s.reserved=s.spectrum_reserved=0;
        s.bluetooth=1;s.bt_reserved[0]=version==5?2:0;s.bt_reserved[1]=version==5?2:0;s.bt_reserved[2]=0;
        unsigned size=version<3?8:version==3?12:16;s.mode=0;
        plat_store_save(OBJ_SETTINGS,&s,size);app_init();app_ui_init();
        assert(gateway_zoom==9 && !gateway_master_wave_auto_scale && gateway_wave_color==1);
        assert(gateway_monitor==(version==5?2:0) && gateway_codec_preference==(version==5?1:3));
    }
    for(unsigned packed=14;packed<256;packed++){
        gateway_settings_t s=gateway_settings();s.zoom=10;s.bt_reserved[2]=packed;
        plat_store_save(OBJ_SETTINGS,&s,sizeof s);app_init();
        assert(gateway_zoom==6 && !gateway_master_wave_auto_scale && gateway_wave_color==1);
    }
    app_ui_init();
    puts("PASS: v1-v5 migrations preserve prior controls with OFF/GREEN defaults; all 244 invalid packed v6 values rejected");
}
static void waveform(void)
{
    static int32_t pcm[512],out[512],ref[512];
    for(unsigned i=0;i<256;i++){pcm[2*i]=768000;pcm[2*i+1]=-1024000;}
    gateway_mode=GATEWAY_MODE_FULL;gateway_monitor=PLAT_MONITOR_MASTER;gateway_zoom=11;gateway_view_changed();
    for(unsigned scale=0;scale<2;scale++){
        gateway_master_wave_auto_scale=scale;master_meter_samples(pcm,256);gateway_capture_wave(now_ms);
        assert(gateway_wave_scale==(scale?4000:32768));
        assert(gateway_wave_y(3000)==(scale?9:32));
        for(unsigned color=0;color<7;color++){
            gateway_wave_color=color;gateway_plot_valid[0]=gateway_plot_valid[1]=0;gateway_draw_wave(0);
            for(unsigned ch=0;ch<2;ch++)for(unsigned x=6;x<234;x++){
                int y=34+78*ch+(scale?(ch?69:9):(ch?39:32));
                assert(fb[y*240+x]==swap16(gateway_wave_colors[color]));
            }
        }
    }
    uint32_t saved_scale=gateway_wave_scale;
    for(unsigned scale=32;scale<=32768;scale*=4){
        gateway_wave_scale=scale;gateway_wave_multiplier=(35u<<20)/scale;
        for(int value=-32768;value<=32767;value++){
            int ref=35-value*35/(int)scale;if(ref<0)ref=0;if(ref>71)ref=71;
            assert(abs((int)gateway_wave_y(value)-ref)<=1);
        }
    }
    gateway_wave_scale=saved_scale;gateway_wave_multiplier=(35u<<20)/saved_scale;
    for(unsigned i=0;i<256;i++){pcm[2*i]=3800*256;pcm[2*i+1]=-3800*256;}
    master_meter_samples(pcm,256);gateway_capture_wave(now_ms);assert(gateway_wave_scale==4000);
    for(unsigned i=0;i<256;i++)pcm[2*i]=pcm[2*i+1]=8000*256;
    master_meter_samples(pcm,256);gateway_capture_wave(now_ms);assert(gateway_wave_scale==8000);
    for(unsigned i=0;i<256;i++)pcm[2*i]=pcm[2*i+1]=4000*256;
    master_meter_samples(pcm,256);gateway_capture_wave(now_ms);assert(gateway_wave_scale<8000 && gateway_wave_scale>7900);
    puts("PASS: reciprocal quantization within 1 pixel across full int16 range/scales, auto-scale dead band, immediate larger peak and slow quiet release");
    gateway_monitor=PLAT_MONITOR_USB;gateway_capture_wave(now_ms);assert(gateway_wave_scale==32768);
    usb_play_buf=pcm;usb_play_n=256;master=2048;plat_master_mute_init(0);
    usb_play_at=0;sim_render(out);
    for(unsigned i=0;i<14;i++){
        gateway_master_wave_auto_scale=i&1;gateway_wave_color=i/2;usb_play_at=0;sim_render(out);
        if(!i)memcpy(ref,out,sizeof ref);else assert(!memcmp(ref,out,sizeof ref));
    }
    usb_play_buf=0;
    puts("PASS: fixed/auto MASTER geometry, all six actual stereo waveform colors, fixed USB scale; audible PCM identical for every setting combination");
}
static void cycles(void)
{
    const int turns[]={1,1,1,-1,-1,-1,8,-8,INT_MAX,INT_MIN,3,-3};
    for(unsigned aac=0;aac<2;aac++)for(unsigned mode=0;mode<3;mode++){
        gateway_monitor=0;gateway_settings_changed();gateway_mode=mode;gateway_aac_active=aac;gateway_view_changed();
        unsigned free=plat_work_available(),source=0,index=0;
        const unsigned order[]={PLAT_MONITOR_USB,PLAT_MONITOR_BT,PLAT_MONITOR_OUT,PLAT_MONITOR_MASTER};
        for(unsigned i=0;i<sizeof turns/sizeof *turns;i++){
            if(!gateway_wide_meters()){index=(index+turns[i]%4+4)%4;source=order[index];}turn(EN_SELECT,turns[i]);
            assert(gateway_monitor==source && plat_work_available()==free);
            if(aac || mode==GATEWAY_MODE_METERS)assert(!gateway_display_block && !usb_input_wave_enabled && !usb_spectrum_enabled);
        }
        if(!aac){now_ms+=750;app_loop();gateway_settings_t s;plat_store_load(OBJ_SETTINGS,&s,sizeof s);assert(s.bt_reserved[1]==source);}
    }
    puts("PASS: forward/reverse cyclic SELECT on all three HOME modes and AAC fallback, accumulated/extreme steps, persistence, no hidden capture or memory growth");
}
int main(void)
{
    palette_set(APP_PALETTE);now_ms=100;app_init();app_ui_init();
    settings();migration();waveform();cycles();assert(!cv_cuts && !cpu_work_depth);
}
