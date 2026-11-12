/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static void tap(int b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();app_loop();}
static void turn(int e,int d){enc_acc[e]=d;app_input_only();app_loop();}
static void profile(void)
{
    uint8_t out[64],discover[]={0,1},caps[]={0,12,4};
    for(unsigned mask=1;mask<16;mask++){
        btr_codec_mask=mask;struct btr_profile p={0};unsigned count=0;
        unsigned n=btr_signal(&p,discover,2,out);assert((out[0]&3)==2);
        for(unsigned c=1;c<=4;c++)if(mask&(1u<<(c-1))){assert(out[2+count*2]==c*4);count++;}
        assert(n==2+2*count);
        for(unsigned c=1;c<=4;c++){
            caps[2]=(uint8_t)(4*c);n=btr_signal(&p,caps,3,out);
            assert((out[0]&3)==(mask&(1u<<(c-1))?2:3));
            if(c<3 || !(mask&(1u<<(c-1))))continue;
            uint8_t set[32]={0,3,(uint8_t)(4*c),4};memcpy(set+4,out+2,n-2);
            /* Capabilities: one frequency is required in SET_CONFIGURATION. */
            set[16]=0x22;n=btr_signal(&p,set,n+2,out);
            assert((out[0]&3)==2 && p.codec==c && p.state==BTR_CONFIGURED && btr_config_rate(&p)==44100);
            uint8_t get[]={0,4,(uint8_t)(4*c)};n=btr_signal(&p,get,3,out);assert(n==(c==3?15:19) && out[7]==255 && out[14]==0x22);
            memset(&p,0,sizeof p);set[16]=0x32;btr_signal(&p,set,c==3?17:21,out);assert((out[0]&3)==3 && !p.state);
            set[16]=0x12;set[10]^=1;btr_signal(&p,set,c==3?17:21,out);assert((out[0]&3)==3);
            memset(&p,0,sizeof p);
        }
    }
}
int main(void)
{
    profile();palette_set(APP_PALETTE);app_init();app_ui_init();app_loop();
    assert(gateway_codec_preference==3 && !strcmp(gateway_settings_value(GATEWAY_SET_BT),"ON"));
    assert(!strcmp(gateway_settings_value(GATEWAY_SET_CODEC),"SELECT"));
    assert(!(lights_b&(1u<<B_SEL)));
    tap(B_EDIT);turn(EN_SELECT,1);turn(EN_ALGO,INT_MAX);assert(gateway_codec_draft==3);
    assert((lights_b&(1u<<B_SEL)) && !(lights_db&(1u<<B_SEL)));
    tap(B_SEL);assert(gateway_codec_dialog && (lights_b&(1u<<B_SEL)));
    unsigned dialog_lights=(1u<<B_OCTDN)|(1u<<B_OCTUP)|(1u<<B_SEL);
    assert((lights_b&dialog_lights)==dialog_lights && !(lights_db&dialog_lights));
    assert(!(lights_b&(1u<<B_SAVE)) && !(lights_db&(1u<<B_SAVE)));
    turn(EN_ALGO,3);assert(!gateway_codec_row);
    tap(B_SEL);assert(gateway_codec_checks==2 && gateway_codec_draft==3 && btr_codec_mask==3);
    turn(EN_SELECT,1);tap(B_SEL);assert(gateway_codec_checks==2 && gateway_codec_empty);
    turn(EN_SELECT,2);assert(gateway_codec_row==3);
    tap(B_SEL);assert(gateway_codec_checks==10 && !gateway_codec_empty);
    gateway_cpu_draft=0;gateway_bt_draft=0;
    gateway_settings_t saved=gateway_saved;unsigned stored=plat_store_size(OBJ_SETTINGS);
    /* Main SAVE cannot confirm a modal. OCT+ updates only its Settings draft,
     * even when persistent storage fails; holding it cannot start pairing. */
    store_fail=1;tap(B_SAVE);assert(gateway_codec_dialog && gateway_codec_draft==3);
    held_btn=1u<<B_OCTUP;app_input_only();now_ms+=1500;app_input_only();
    assert(!gateway_oct_tracking && !plat_bt_receiver_pairing());
    held_btn=0;app_input_only();app_loop();
    assert(!gateway_codec_dialog && !gateway_save_notice && gateway_codec_draft==10 && btr_codec_mask==3);
    assert((lights_b&(1u<<B_SAVE)) && !(lights_db&(1u<<B_SAVE)));
    assert(gateway_bt_mode==1 && gateway_cpu_mode==1 && plat_store_size(OBJ_SETTINGS)==stored && !memcmp(&gateway_saved,&saved,sizeof saved));
    static uint16_t closed[240*240];memcpy(closed,fb,sizeof closed);
    lcd_fill(0,18,240,222,C_BLACK);gateway_settings_draw();assert(!memcmp(closed+18*240,fb+18*240,222*240*2));
    /* OCT- cancels just the checklist, stays on Settings and restores LEDs. */
    tap(B_SEL);tap(B_SEL);assert(gateway_codec_checks==11 && gateway_codec_draft==10);
    tap(B_OCTDN);assert(!gateway_codec_dialog && gateway_screen==GATEWAY_SETTINGS && gateway_codec_draft==10);
    assert(!(lights_b&((1u<<B_OCTDN)|(1u<<B_OCTUP))) && (lights_db&(1u<<B_OCTUP)));
    tap(B_SAVE);assert(gateway_save_notice==2 && btr_codec_mask==3 && gateway_bt_mode==1);
    store_fail=0;tap(B_SAVE);assert(gateway_save_notice==1 && btr_codec_mask==10 && !gateway_bt_mode && !gateway_cpu_mode);
    assert(!(lights_b&(1u<<B_SAVE)) && (lights_db&(1u<<B_SAVE)));
    /* Every screen button cancels the checklist without confirming changes. */
    const unsigned exit_buttons[]={B_HOME,B_GLO,B_FX,B_EDIT};
    for(unsigned i=0;i<4;i++){
        if(gateway_screen!=GATEWAY_SETTINGS)tap(B_EDIT);
        assert(gateway_settings_row==GATEWAY_SET_CODEC && gateway_codec_draft==10);
        tap(B_SEL);tap(B_SEL);assert(gateway_codec_checks==11 && gateway_codec_draft==10);
        tap(exit_buttons[i]);assert(!gateway_codec_dialog && gateway_codec_draft==10 && btr_codec_mask==10);
        unsigned screen=i==0?GATEWAY_MAIN:i==1?GATEWAY_MIXER:i==2?GATEWAY_FX:GATEWAY_SETTINGS;
        assert(gateway_screen==screen);
        if(screen!=GATEWAY_FX)assert(!!(lights_b&(1u<<B_SEL))==(screen==GATEWAY_SETTINGS));
    }
    turn(EN_SELECT,1);assert(!(lights_b&(1u<<B_SEL)));
    turn(EN_SELECT,-1);assert(lights_b&(1u<<B_SEL));
    /* An uncommitted checklist draft never survives reboot. */
    tap(B_SEL);tap(B_SEL);tap(B_OCTUP);assert(gateway_codec_draft==11 && btr_codec_mask==10);
    app_init();app_ui_init();assert(gateway_codec_preference==10);
    gateway_settings_t s=gateway_settings();s.version=15;s.bt_reserved[0]=0;s.bt_reserved[0]=2;s.zoom=9;
    plat_store_save(OBJ_SETTINGS,&s,24);app_init();app_ui_init();assert(gateway_zoom==9 && gateway_codec_preference==1);
    s.version=16;s.bt_reserved[0]=0;plat_store_save(OBJ_SETTINGS,&s,24);app_init();app_ui_init();assert(gateway_codec_preference==3 && gateway_zoom==6);
    btr_host_on=btr_host_connected=1;
    for(unsigned codec=PLAT_BT_CODEC_APTX;codec<=PLAT_BT_CODEC_APTX_HD;codec++){
        btr_host_codec=(uint8_t)codec;gateway_screen=GATEWAY_MAIN;
        gateway_mode=GATEWAY_MODE_FULL;gateway_view_changed();now_ms+=500;app_loop();assert(gateway_has_wave() && !gateway_aac_active);
        gateway_mode=GATEWAY_MODE_SPECTRUM;gateway_view_changed();now_ms+=500;app_loop();assert(gateway_has_spectrum());
    }
    gateway_mode=GATEWAY_MODE_METERS;gateway_view_changed();
    unsigned available=plat_work_available();
    void *reserved=plat_work_alloc_top(available-120000);assert(reserved);
    void *record=plat_work_alloc(gateway_usb_out_bytes(GATEWAY_USB_BT_FX));assert(record);
    void *aptx=plat_work_alloc_top(12768);assert(aptx);
    for(unsigned i=0;i<50;i++){
        gateway_mode=GATEWAY_MODE_FULL;gateway_view_changed();assert(gateway_has_wave());
        gateway_mode=GATEWAY_MODE_SPECTRUM;gateway_view_changed();assert(gateway_has_spectrum());
    }
    gateway_mode=GATEWAY_MODE_METERS;gateway_view_changed();plat_work_free(aptx);plat_work_free(record);plat_work_free(reserved);
    assert(plat_work_available()==available);
    assert(!cv_cuts && !cpu_work_depth);
    puts("PASS: four-codec negotiation, OCT-/OCT+ checklist cancel/confirm, draft-only OK, modal lights/pairing isolation, navigation cancel, persistence/migration and aptX graph availability");
}
