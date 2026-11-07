/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static uint16_t screen[240*240];
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void turn(unsigned e,int d){enc_acc[e]=d;app_input_only();}
static unsigned pixels(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t color){unsigned n=0;for(unsigned j=y;j<y+h;j++)for(unsigned i=x;i<x+w;i++)n+=fb[j*240+i]==swap16(color);return n;}
static void settings(void){
 for(unsigned v=1;v<=8;v++){
  gateway_settings_t s={{'A','R','T'},v,9,0,0,0,1,0,3,0,0,{0,0,0}};
  if(v==8){s.spectrum_reserved=6;s.bt_reserved[2]=13;s.reserved=1;}
  plat_store_save(OBJ_SETTINGS,&s,v<3?8:v==3?12:16);app_init();app_ui_init();assert(gateway_cpu_mode && gateway_zoom==9);
 }
 tap(B_EDIT);turn(EN_SELECT,INT_MIN);turn(EN_SELECT,GATEWAY_SET_CPU);assert(gateway_settings_row==GATEWAY_SET_CPU && GATEWAY_SETTING_ROWS==9);
 turn(EN_ALGO,-1);assert(!gateway_cpu_draft && gateway_cpu_mode);
 store_fail=1;tap(B_SAVE);assert(gateway_cpu_mode && gateway_save_notice==2);
 store_fail=0;tap(B_SAVE);assert(!gateway_cpu_mode && gateway_save_notice==1);app_loop();
 gateway_settings_t s;assert(plat_store_load(OBJ_SETTINGS,&s,sizeof s)==sizeof s && s.version==15 && s.reserved==3);
 assert(pixels(0,218,240,20,RGB(255,40,40))>10);
 gateway_draw_cpu();for(unsigned y=0;y<18;y++)for(unsigned x=130;x<195;x++)assert(!fb[y*240+x]);
 assert(pixels(195,0,45,18,C_WHITE)>10); /* battery remains */
 app_init();app_ui_init();assert(!gateway_cpu_mode && gateway_battery_mode);
 tap(B_EDIT);turn(EN_SELECT,INT_MIN);turn(EN_SELECT,GATEWAY_SET_CPU);turn(EN_ALGO,1);tap(B_HOME);tap(B_EDIT);assert(!gateway_cpu_draft); /* discard */
 turn(EN_SELECT,INT_MIN);turn(EN_SELECT,GATEWAY_SET_CPU);turn(EN_ALGO,1);tap(B_SAVE);app_loop();assert(gateway_cpu_mode);
 s=gateway_settings();s.reserved=28;plat_store_save(OBJ_SETTINGS,&s,sizeof s);app_init();app_ui_init();assert(gateway_cpu_mode && gateway_zoom==6);
 tap(B_EDIT);app_loop();
 assert(fb[42*240+237]==swap16(RGB(160,160,160)) && fb[208*240+237]==swap16(RGB(35,35,35)));
 turn(EN_SELECT,INT_MAX);app_loop();
 assert(fb[42*240+237]==swap16(RGB(35,35,35)) && fb[208*240+239]==swap16(RGB(160,160,160)));
 assert(!fb[42*240] && !fb[42*240+234]);
}
static void fx(void){
 tap(B_FX);turn(EN_SELECT,-1);assert(gateway_fx_row==1);app_loop();
 assert(!fb[155*240] && !fb[155*240+100]);
 char text[40];gateway_fx_format(text,"post EQ gain: ",gateway_fx_params.post,1,"dB");int x=237-text_w(&FONT_T,text)-3;
 assert(fb[155*240+x]==swap16(RGB(75,210,150)) && !fb[155*240+x-1]);
 for(unsigned lim=0;lim<2;lim++){
  if(lim)tap(B_FX);gateway_fx_notice=0;gateway_page_redraw=1;app_loop();memcpy(screen,fb,sizeof screen);
  for(unsigned notice=1;notice<=2;notice++){
   gateway_fx_notice=notice;gateway_page_redraw=1;app_loop();assert(!memcmp(screen+18*240,fb+18*240,16*240*sizeof(uint16_t)));
  }
 }
}
static void footer(void){
 tap(B_HOME);gateway_monitor=PLAT_MONITOR_USB;
 for(unsigned mode=GATEWAY_MODE_FULL;mode<=GATEWAY_MODE_SPECTRUM;mode++){
  gateway_mode=mode;gateway_view_changed();app_loop();gateway_meter[0].held=540;gateway_draw_meter(1);
  unsigned caption_y=mode==GATEWAY_MODE_SPECTRUM?182:196;
  assert(pixels(6,196,70,12,RGB(96,96,96))>10 && pixels(80,caption_y,100,12,RGB(96,96,96))>5);
  assert(!pixels(6,196,180,12,C_WHITE) && pixels(210,196,30,12,C_WHITE)>0);
  if(mode==GATEWAY_MODE_SPECTRUM){
   assert(GATEWAY_SPECTRUM_TOP==26 && GATEWAY_SPECTRUM_BOTTOM==162);
   memcpy(screen,fb,sizeof screen);cv_begin(240,14,C_BLACK);
   const char *caption="MIN -90 MAX 0 MID";
   cv_text((240-text_w(&FONT_T,caption))/2,0,&FONT_T,caption,RGB(96,96,96));cv_blit(0,182);
   for(unsigned y=182;y<196;y++)assert(!memcmp(screen+y*240,fb+y*240,240*sizeof(uint16_t)));
  }
 }
}
int main(void){palette_set(APP_PALETTE);now_ms=1000;app_init();app_ui_init();settings();fx();footer();assert(!cv_cuts && !cpu_work_depth);puts("PASS: v1-v8 migration/v10 CPU setting, save failure/discard/reboot, scrolling settings, battery-only header, post text-only highlight, no FX save notices, dim metadata/bright peaks and spectrum caption above source/peak");}
