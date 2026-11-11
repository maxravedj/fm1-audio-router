/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void turn(unsigned e,int d){enc_acc[e]=d;app_input_only();}
static void setrow(unsigned row){turn(EN_SELECT,INT_MIN);turn(EN_SELECT,row);assert(gateway_settings_row==row);}
static uint16_t screen[240*240];
static void main_select(void){
 gateway_mode=GATEWAY_MODE_METERS;gateway_monitor=PLAT_MONITOR_USB;gateway_view_changed();
 unsigned writes=store_writes;turn(EN_SELECT,1);turn(EN_SELECT,INT_MIN);assert(!gateway_monitor && store_writes==writes);
 gateway_mode=GATEWAY_MODE_FULL;gateway_view_changed();turn(EN_SELECT,1);assert(gateway_monitor==PLAT_MONITOR_BT);
 gateway_mode=GATEWAY_MODE_METERS;gateway_view_changed();
}
static void migration(void){
 for(unsigned v=1;v<=7;v++){
  gateway_settings_t s={{'A','R','T'},v,9,1,0,0,1,0,3,0,0,{0,0,0}};
  if(v>=6)s.bt_reserved[2]=11;
  unsigned size=v<3?8:v==3?12:16;plat_store_save(OBJ_SETTINGS,&s,size);app_init();app_ui_init();
  assert(gateway_zoom==9 && gateway_muted && gateway_spectrum_color==GATEWAY_WAVE_YELLOW && gateway_battery_mode);
  if(v>=6)assert(gateway_wave_color==5 && gateway_master_wave_auto_scale);
 }
 gateway_settings_t s=gateway_settings();s.reserved=28;plat_store_save(OBJ_SETTINGS,&s,sizeof s);app_init();app_ui_init();assert(gateway_zoom==6);
 s=gateway_settings();s.spectrum_reserved=7;plat_store_save(OBJ_SETTINGS,&s,sizeof s);app_init();app_ui_init();assert(gateway_spectrum_color==3);
}
static void settings(void){
 tap(B_EDIT);assert(GATEWAY_SETTING_ROWS==9);
 setrow(GATEWAY_SET_COLOR);turn(EN_ALGO,99);assert(gateway_color_draft==6 && gateway_wave_color==2);
 setrow(GATEWAY_SET_SPECTRUM_COLOR);turn(EN_ALGO,99);assert(gateway_spectrum_color_draft==6 && gateway_spectrum_color==3);
 setrow(GATEWAY_SET_BATTERY);turn(EN_ALGO,-1);assert(!gateway_battery_draft && gateway_battery_mode);
 store_fail=1;tap(B_SAVE);assert(gateway_save_notice==2 && gateway_wave_color==2 && gateway_battery_mode);
 store_fail=0;tap(B_SAVE);assert(gateway_wave_color==6 && gateway_spectrum_color==6 && !gateway_battery_mode);
 gateway_settings_t s;assert(plat_store_load(OBJ_SETTINGS,&s,sizeof s)==sizeof s && s.version==17 && s.spectrum_reserved==6 && !s.reserved);
 app_init();app_ui_init();assert(gateway_wave_color==6 && gateway_spectrum_color==6 && !gateway_battery_mode);
 for(unsigned color=0;color<7;color++){
  tap(B_EDIT);setrow(GATEWAY_SET_SPECTRUM_COLOR);turn(EN_ALGO,-99);turn(EN_ALGO,color);tap(B_SAVE);
  assert(gateway_spectrum_color==color && gateway_spectrum_color_value()==gateway_wave_colors[color]);
 }
 tap(B_EDIT);setrow(GATEWAY_SET_COLOR);turn(EN_ALGO,-99);tap(B_HOME);tap(B_EDIT);assert(gateway_color_draft==6);
 tap(B_HOME);
}
static void spectrum_footer(void){
 gateway_screen=GATEWAY_MAIN;gateway_mode=GATEWAY_MODE_SPECTRUM;gateway_monitor=PLAT_MONITOR_USB;
 gateway_spectrum_max=24;gateway_spectrum_min=54;gateway_spectrum_rate=1;gateway_view_changed();app_loop();gateway_draw_meter(1);
 memcpy(screen,fb,sizeof screen);
 cv_begin(120,12,C_BLACK);const char *expected="MIN -54 MAX -24 MID";
 cv_text((120-text_w(&FONT_T,expected))/2,0,&FONT_T,expected,RGB(96,96,96));cv_blit(60,182);
 for(unsigned y=182;y<194;y++)assert(!memcmp(screen+y*240+60,fb+y*240+60,120*sizeof(uint16_t)));
}
static void fx(void){
 tap(B_FX);assert(gateway_fx_row==2);turn(EN_SELECT,-1);assert(gateway_fx_row==1);
 app_loop();assert(fb[155*240]==0 && fb[155*240+230]!=0);
 turn(EN_SELECT,-1);assert(gateway_fx_row==6);turn(EN_SELECT,1);assert(gateway_fx_row==1);
 for(unsigned row=1;row<=6;row++){
  gateway_fx_row=row;unsigned chain=gateway_fx_params.chain,eq=gateway_fx_params.eq;
  for(unsigned e=0;e<NE;e++)if(e!=EN_SELECT){turn(e,-1);turn(e,1);}
  assert(gateway_fx_params.chain==chain && gateway_fx_params.eq==eq);
 }
 for(unsigned i=0;i<5;i++)assert(gateway_fx_params.band[i].on);
 tap(B_SEL);assert(!gateway_fx_params.eq);tap(B_SEL);assert(gateway_fx_params.eq);
 gateway_fx_params.chain=1;gateway_page_redraw=1;app_loop();memcpy(screen,fb,sizeof screen);
 gateway_fx_params.chain=0;gateway_page_redraw=1;app_loop();
 assert(!memcmp(screen+34*240,fb+34*240,(240-34)*240*sizeof(uint16_t)));
 tap(B_FX);unsigned lim=gateway_fx_params.lim;
 for(unsigned e=0;e<NE;e++)if(e!=EN_SELECT){turn(e,-1);turn(e,1);}assert(gateway_fx_params.lim==lim && !gateway_fx_params.chain);
 tap(B_SEL);assert(gateway_fx_params.lim!=lim);tap(B_SEL);
 held_btn=1u<<B_FX;app_input_only();now_ms+=1000;app_input_only();assert(gateway_fx_params.chain);
 held_btn=0;app_input_only();assert(gateway_fx_limiter);
}
static void badges(void){
 gateway_bt_mode=btr_host_on=1;btr_host_connected=0;gateway_bar_key=~0u;gateway_draw_cpu();
 unsigned bt_x=3+text_w(&FONT_T,"USB IN")+3+2;
 unsigned narrow=text_w(&FONT_T,"BT")+3;assert(fb[2*240+bt_x+narrow+2]==swap16(RGB(20,150,50)));
 btr_host_connected=1;btr_host_codec=PLAT_BT_CODEC_SBC;gateway_draw_cpu();
 unsigned wide=text_w(&FONT_T,"BT SBC")+3;assert(wide>narrow && fb[2*240+bt_x+wide+2]==swap16(RGB(20,150,50)));
 assert(fb[2*240+bt_x+narrow+2]==swap16(RGB(20,150,50))); /* inside wider BT badge */
 cv_begin(60,18,C_BLACK);gateway_badge(0,60,"BT",RGB(20,150,50),0);cv_blit(0,0);
 unsigned black=0;for(unsigned y=4;y<15;y++)for(unsigned x=24;x<36;x++)black+=fb[y*240+x]==0;assert(black>10);
}
int main(void){palette_set(APP_PALETTE);now_ms=1000;app_init();app_ui_init();main_select();migration();settings();spectrum_footer();fx();badges();assert(!cv_cuts && !cpu_work_depth);puts("PASS: v1-v7 migration/v8 strict settings, 7 colors, atomic drafts, battery toggle, post selection/wrap, knobs never toggle effects, SEL/long FX, global-independent EQ appearance and compact black-text badges");}
