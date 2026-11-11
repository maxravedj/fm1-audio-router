/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();app_loop();}
static void turn(unsigned e,int d){enc_acc[e]=d;app_input_only();app_loop();}
static void status(const char *s,int dirty)
{
    assert(!strcmp(gateway_settings_status(),s));
    assert(!!(lights_b&(1u<<B_SAVE))==dirty);
    assert(!!(lights_db&(1u<<B_SAVE))==!dirty);
    /* Centered FONT_XS: successful saves green; unsaved/failed messages red. */
    uint16_t actual[240*22];memcpy(actual,fb+218*240,sizeof actual);
    lcd_fill(0,218,240,22,C_BLACK);draw_text_box(0,218,240,&FONT_XS,s,!strcmp(s,"SAVED")?RGB(40,220,60):RGB(255,40,40),1);
    assert(!memcmp(actual,fb+218*240,sizeof actual));
}
int main(void)
{
    app_init();app_ui_init();app_loop();tap(B_EDIT);status("",0);
    unsigned writes=store_writes;
    /* Every draft row lights SAVE immediately. Reversing an edit returns to
     * clean, with neither a flash write nor an applied audio/radio change. */
    for(unsigned row=0;row<GATEWAY_SETTING_ROWS;row++){
        if(row==GATEWAY_SET_CODEC)continue;
        turn(EN_SELECT,(int)row-gateway_settings_row);
        uint8_t old=row==GATEWAY_SET_BT?gateway_bt_draft:row==GATEWAY_SET_AUTO_SCALE?gateway_auto_scale_draft:
            row==GATEWAY_SET_COLOR?gateway_color_draft:row==GATEWAY_SET_SPECTRUM_COLOR?gateway_spectrum_color_draft:
            row==GATEWAY_SET_BATTERY?gateway_battery_draft:row==GATEWAY_SET_CPU?gateway_cpu_draft:
            row==GATEWAY_SET_USB_OUT?gateway_usb_out_draft:gateway_fade_draft;
        int delta=old?-1:1;turn(EN_ALGO,delta);status("UNSAVED",1);
        turn(EN_ALGO,-delta);status("",0);assert(store_writes==writes);
    }
    turn(EN_SELECT,(int)GATEWAY_SET_CODEC-gateway_settings_row);tap(B_SEL);
    assert(gateway_codec_dialog && !(lights_b&(1u<<B_SAVE)));
    /* All five labels are visible together, even before moving the cursor. */
    for(unsigned row=0;row<5;row++){
        uint16_t actual[140*16];unsigned y=26+32+21*row+3;
        for(unsigned i=0;i<16;i++)memcpy(actual+140*i,fb+(y+i)*240+51,280);
        cv_begin(140,16,row?RGB(15,22,30):RGB(25,45,65));
        cv_text(0,0,&FONT_XS,gateway_codecs[row],C_WHITE);cv_blit(51,y);
        for(unsigned i=0;i<16;i++)assert(!memcmp(actual+140*i,fb+(y+i)*240+51,280));
    }
    tap(B_SEL);tap(B_OCTUP);status("UNSAVED",1);assert(store_writes==writes);
    store_fail=1;tap(B_SAVE);status("SAVE FAILED",1);assert(gateway_codec_preference==3);
    store_fail=0;tap(B_SAVE);status("SAVED",0);assert(gateway_codec_preference==2);
    turn(EN_SELECT,-1);turn(EN_ALGO,-1);status("UNSAVED",1);
    tap(B_HOME);assert(!(lights_b&(1u<<B_SAVE)));tap(B_EDIT);status("",0);
    puts("PASS: all Settings draft rows, immediate full SAVE LED, revert/save/failure/discard, green SAVED/red UNSAVED/failed status pixels and five codec labels visible");
}
