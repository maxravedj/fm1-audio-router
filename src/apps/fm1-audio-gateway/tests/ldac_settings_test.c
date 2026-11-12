/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static void tap(int b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();app_loop();}
static void turn(int e,int n){enc_acc[e]=n;app_input_only();app_loop();}
static void profile(void)
{
    uint8_t out[64],discover[]={0,1},caps[]={0,12,20};
    for(unsigned mask=1;mask<=31;mask++){
        btr_codec_mask=(uint8_t)mask;struct btr_profile p={0};
        unsigned n=btr_signal(&p,discover,2,out),count=0;
        for(unsigned c=1;c<=5;c++)if(mask&(1u<<(c-1)))assert(out[2+2*count++]==4*c);
        assert(n==2+2*count && (out[0]&3)==2);
        n=btr_signal(&p,caps,3,out);assert((out[0]&3)==(mask&16?2:3));
        if(!(mask&16))continue;
        assert(n==16 && out[7]==255 && out[8]==0x2d && out[9]==1 && out[12]==0xaa && out[14]==0x30 && out[15]==1);
        for(unsigned rate=0;rate<2;rate++){
            uint8_t set[18]={0,3,20,4};memcpy(set+4,out+2,14);set[16]=rate?0x10:0x20;
            memset(&p,0,sizeof p);n=btr_signal(&p,set,sizeof set,out);
            assert(n==2 && (out[0]&3)==2 && p.codec==BTR_CODEC_LDAC && p.seid==20 && btr_config_rate(&p)==(rate?48000:44100));
            uint8_t get[]={0,4,20};n=btr_signal(&p,get,3,out);
            assert(n==16 && out[14]==set[16] && out[15]==1);
            uint8_t open[]={0,6,20},start[]={0,7,20};
            btr_signal(&p,open,3,out);p.media=1;btr_signal(&p,start,3,out);assert(p.state==BTR_STREAMING);
            uint8_t suspend[]={0,9,20},reconf[]={0,5,20,7,10,0,255,0x2d,1,0,0,0xaa,0,0x20,1};
            btr_signal(&p,suspend,3,out);btr_signal(&p,reconf,sizeof reconf,out);assert((out[0]&3)==2 && btr_config_rate(&p)==44100);
            for(unsigned bad=0;bad<6;bad++){
                memset(&p,0,sizeof p);set[16]=0x20;set[17]=1;
                if(bad==0)set[16]=0x30;if(bad==1)set[16]=4;
                if(bad==2)set[17]=3;if(bad==3)set[17]=2;
                if(bad==4)set[10]^=1;
                btr_signal(&p,set,bad==5?sizeof set-1:sizeof set,out);assert((out[0]&3)==3 && p.state==BTR_IDLE);
                if(bad==4)set[10]^=1;
            }
            btr_signal(&p,caps,3,out); /* restore capabilities for next rate */
        }
    }
}
int main(void)
{
    profile();palette_set(APP_PALETTE);app_init();app_ui_init();app_loop();
    tap(B_EDIT);turn(EN_SELECT,1);tap(B_SEL);turn(EN_SELECT,INT_MAX);
    assert(gateway_codec_row==4 && !strcmp(gateway_codecs[4],"LDAC"));
    tap(B_SEL);assert(gateway_codec_checks==19 && gateway_codec_draft==3);
    tap(B_OCTUP);assert(gateway_codec_draft==19 && btr_codec_mask==3);
    tap(B_SAVE);assert(gateway_codec_preference==19 && btr_codec_mask==19);
    gateway_settings_t s;assert(plat_store_load(OBJ_SETTINGS,&s,sizeof s)==sizeof s && s.version==GATEWAY_SETTINGS_VERSION && s.bt_reserved[0]==19);
    app_init();app_ui_init();assert(gateway_codec_preference==19);
    s.version=16;s.bt_reserved[0]=15;plat_store_save(OBJ_SETTINGS,&s,24);app_init();app_ui_init();assert(gateway_codec_preference==15);
    s.version=17;s.bt_reserved[0]=16;plat_store_save(OBJ_SETTINGS,&s,24);app_init();app_ui_init();assert(gateway_codec_preference==16);
    /* Model the actual guarded target budget with recording already reserved.
     * Decoder priority detaches capture before scratch is reused. */
    gateway_screen=GATEWAY_MAIN;gateway_mode=GATEWAY_MODE_SPECTRUM;gateway_view_changed();
    assert(gateway_has_spectrum());btr_host_on=btr_host_connected=1;btr_host_codec=PLAT_BT_CODEC_LDAC;
    app_bt_ldac_prepare();assert(!gateway_display_block && !usb_spectrum_ring && !usb_wave_history);
    void *limit=plat_work_alloc_top(plat_work_available()-102304);assert(limit);
    void *record=plat_work_alloc_top(gateway_usb_out_bytes(GATEWAY_USB_BT_FX));assert(record);
    void *decoder=plat_work_alloc_top(23840);assert(decoder);btr_host_ldac_ready=1;
    app_loop();assert(!gateway_display_block && gateway_wide_meters());
    /* A paused configured session reserves the same decoder, without media.
     * From a spectrum fallback, one HOME press reaches the real waveform. */
    tap(B_HOME);assert(gateway_has_wave());
    for(unsigned i=0;i<10;i++){
        tap(B_HOME);assert(gateway_mode==GATEWAY_MODE_METERS && !gateway_display_block);
        tap(B_HOME);assert(gateway_has_wave());
    }
    tap(B_EDIT);tap(B_HOME);assert(gateway_has_wave());
    /* If even waveform won't fit, keep telemetry visible and retry on release. */
    gateway_mode=GATEWAY_MODE_METERS;gateway_view_changed();
    void *pressure=plat_work_alloc_top(20000);assert(pressure);
    gateway_mode=GATEWAY_MODE_FULL;gateway_view_changed();assert(!gateway_display_block && gateway_wide_meters());
    tap(B_HOME);assert(gateway_mode==GATEWAY_MODE_METERS && !gateway_display_block);
    plat_work_free(pressure);tap(B_HOME);assert(gateway_has_wave());
    app_bt_ldac_prepare();btr_host_ldac_ready=0;
    plat_work_free(decoder);btr_host_connected=0;gateway_mode=GATEWAY_MODE_SPECTRUM;
    app_loop();assert(gateway_has_spectrum() && !usb_spectrum_filled);
    gateway_mode=GATEWAY_MODE_METERS;gateway_view_changed();plat_work_free(record);plat_work_free(limit);
    assert(plat_work_available()==sizeof work_host_storage && !cv_cuts && !cpu_work_depth);
    puts("PASS: all 31 codec masks, LDAC endpoint/configuration/reconfiguration/bounds, five-row draft/LED settings and v16/v17 restore; decoder-first allocation, HOME skips unavailable graphs, paused waveform access, exhaustion and fresh disconnect recovery");
}
