/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static void turn(unsigned k,int delta){enc_acc[k]=delta;app_input_only();}
static void start(void){app_init();app_ui_init();gateway_mode=GATEWAY_MODE_SPECTRUM;gateway_view_changed();}
static void defaults(void){assert(gateway_spectrum_min==90 && !gateway_spectrum_max && gateway_spectrum_rate==1);}
int main(void)
{
    palette_set(APP_PALETTE);now_ms=100;start();defaults();
    turn(EN_K1,12);assert(gateway_spectrum_min==78 && !gateway_spectrum_max);
    turn(EN_K2,-24);assert(gateway_spectrum_max==24 && gateway_spectrum_min==78);
    turn(EN_K3,1);assert(gateway_spectrum_rate==0);
    turn(EN_K3,INT_MAX);assert(!gateway_spectrum_rate);turn(EN_K3,INT_MIN);assert(gateway_spectrum_rate==2);
    turn(EN_K1,INT_MAX);assert(gateway_spectrum_min==25); /* cannot cross MAX */
    turn(EN_K2,INT_MIN);assert(gateway_spectrum_max==24);
    turn(EN_K1,INT_MIN);assert(gateway_spectrum_min==150);
    turn(EN_K2,INT_MIN);assert(gateway_spectrum_max==149);
    turn(EN_K2,INT_MAX);assert(!gateway_spectrum_max);
    turn(EN_K1,INT_MAX);assert(gateway_spectrum_min==1);
    /* Every legal independent pair maps exactly and monotonically. */
    for(unsigned min=1;min<=150;min++)for(unsigned max=0;max<min;max++) {
        gateway_spectrum_min=min;gateway_spectrum_max=max;
        int bottom=-(int)min*1000,top=-(int)max*1000;
        assert(gateway_spectrum_bottom_db()==bottom && gateway_spectrum_top_db()==top);
        assert(!gateway_spectrum_height(bottom) && gateway_spectrum_height(top)==136);
        assert(!gateway_spectrum_height(bottom-1) && gateway_spectrum_height(top+1)==136);
        unsigned previous=0;
        for(int db=-150000;db<=0;db+=1000){unsigned h=gateway_spectrum_height(db);assert(h>=previous && h<=136);previous=h;}
    }
    gateway_spectrum_min=90;gateway_spectrum_max=24;gateway_spectrum_rate=2;
    unsigned writes=store_writes;now_ms=849;app_loop();assert(store_writes==writes);
    now_ms=850;app_loop();assert(store_writes==writes+1);
    gateway_settings_t s;assert(plat_store_load(OBJ_SETTINGS,&s,sizeof s)==24 && s.version==17 && s.spectrum_range==90);
    start();assert(gateway_spectrum_min==90 && gateway_spectrum_max==24 && gateway_spectrum_rate==2);
    /* Full legacy range -> identical absolute dB window, all old combinations. */
    for(unsigned range=0;range<4;range++)for(unsigned max=0;max<=60;max++) {
        s=gateway_settings();s.version=12;s.bt_reserved[0]=0;s.spectrum_max=max;s.spectrum_range=range;
        assert(!plat_store_save(OBJ_SETTINGS,&s,22));app_init();
        assert(gateway_spectrum_min==max+gateway_spectrum_legacy_ranges[range] && gateway_spectrum_max==max);
    }
    for(unsigned version=3;version<=12;version++) {
        s=gateway_settings();s.version=version;s.bt_reserved[0]=0;s.spectrum_max=24;s.spectrum_range=1;
        s.reserved=version<8?0:1;s.spectrum_reserved=0;s.bt_reserved[0]=s.bt_reserved[1]=s.bt_reserved[2]=0;
        unsigned size=version==3?12:version<10?16:version<12?20:22;
        assert(!plat_store_save(OBJ_SETTINGS,&s,size));app_init();
        assert(gateway_spectrum_min==54 && gateway_spectrum_max==24);
    }
    for(unsigned version=1;version<=2;version++) {
        s=gateway_settings();s.version=version;s.bt_reserved[0]=0;s.mode=0;s.reserved=0;
        assert(!plat_store_save(OBJ_SETTINGS,&s,8));app_init();defaults();
    }
    for(unsigned bad=0;bad<5;bad++) {
        s=gateway_settings();s.spectrum_range=90;s.spectrum_max=0;
        if(bad==0)s.spectrum_range=0;if(bad==1)s.spectrum_range=151;
        if(bad==2)s.spectrum_max=90;if(bad==3)s.spectrum_max=150;if(bad==4)s.spectrum_rate=3;
        assert(!plat_store_save(OBJ_SETTINGS,&s,sizeof s));app_init();defaults();
    }
    /* Editing changes display mapping, never capture/history/held levels. */
    start();gateway_spectrum_level[0]=-50000;gateway_spectrum_peak[0]=-30000;
    uint32_t count=usb_spectrum_count;turn(EN_K1,10);turn(EN_K2,-10);turn(EN_K3,1);
    assert(gateway_spectrum_level[0]==-50000 && gateway_spectrum_peak[0]==-30000 && usb_spectrum_count==count);
    gateway_screen=GATEWAY_SETTINGS;gateway_view_changed();unsigned min=gateway_spectrum_min;
    turn(EN_K1,5);assert(gateway_spectrum_min==min);
    /* Source and peak have their own footer row; the entire MIN/MAX/decay
     * caption is centered above, without overlap even at extreme windows. */
    gateway_screen=GATEWAY_MAIN;gateway_mode=GATEWAY_MODE_SPECTRUM;
    gateway_monitor=PLAT_MONITOR_MASTER;gateway_view_changed();
    for(unsigned narrow=0;narrow<2;narrow++) {
        gateway_spectrum_min=narrow?150:90;gateway_spectrum_max=narrow?149:0;gateway_spectrum_rate=2;
        gateway_draw_meter(1);
        static uint16_t actual[240*240];memcpy(actual,fb,sizeof actual);
        const char *caption=narrow?"MIN -150 MAX -149 SLOW":"MIN -90 MAX 0 SLOW";
        char peak[8];gateway_peak_label(peak,gateway_meter[0].peak>gateway_meter[1].peak?gateway_meter[0].peak:gateway_meter[1].peak);
        for(unsigned band=0;band<26;band+=16){
            unsigned h=26-band;if(h>16)h=16;cv_begin(240,h,C_BLACK);cv_oy=-(int)band;
            cv_text(6,14,&FONT_T,"Source: MASTER",RGB(96,96,96));
            cv_text(234-text_w(&FONT_T,peak),14,&FONT_T,peak,C_WHITE);
            cv_text((240-text_w(&FONT_T,caption))/2,0,&FONT_T,caption,RGB(96,96,96));
            cv_oy=0;cv_blit(0,182+band);
        }
        for(unsigned y=182;y<208;y++)assert(!memcmp(actual+y*240,fb+y*240,240*sizeof(uint16_t)));
    }
    puts("PASS: K1 MIN / K2 MAX / K3 decay, crossing/extreme clamps, 11325 legal windows, current save/restore and v1-v12 migration, no capture/peak reset, exact two-row footer pixels");
}
