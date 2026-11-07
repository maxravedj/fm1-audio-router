/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static uint16_t saved[240*240];
static void header(unsigned g,const char *peak,const char *gain){
 unsigned base=20+g*55;memcpy(saved,fb,sizeof saved);cv_begin(55,16,C_BLACK);
 cv_text(17-text_w(&FONT_T,peak)/2,0,&FONT_T,peak,C_WHITE);
 cv_text(44-text_w(&FONT_T,gain)/2,0,&FONT_T,gain,g<3?RGB(80,170,250):C_WHITE);cv_blit(base,40);
 for(unsigned y=40;y<56;y++)assert(!memcmp(fb+y*240+base,saved+y*240+base,55*sizeof(uint16_t)));
}
static void scale(void){
 /* Independent pixel oracle: all seven Home-1 values, right-aligned to x18. */
 static const char *labels[]={"0","-6","-12","-24","-36","-48","-60"};
 static const unsigned tops[]={0,15,29,58,87,116,145};
 memcpy(saved,fb,sizeof saved);cv_begin(20,158,C_BLACK);
 for(unsigned i=0;i<7;i++)cv_text(18-text_w(&FONT_T,labels[i]),tops[i],&FONT_T,labels[i],C_GRAY);
 cv_blit(0,58);
 for(unsigned y=58;y<216;y++)assert(!memcmp(fb+y*240,saved+y*240,20*sizeof(uint16_t)));
}
int main(void){
 palette_set(APP_PALETTE);now_ms=1000;app_init();app_ui_init();gateway_screen=GATEWAY_MIXER;gateway_view_changed();app_loop();
 const unsigned pairs[]={0,4,6,2};
 for(unsigned g=0;g<4;g++)for(unsigned ch=0;ch<2;ch++)gateway_meter[pairs[g]+ch]=(gateway_meter_t){540-60*g,540-60*g,750,0,0,0};
 gateway_mix_db[0]=-120;gateway_mix_db[1]=GATEWAY_GAIN_OFF;master=2048;gateway_mixer_draw(1);
 header(0,"-6.0","-12.0");header(1,"-12.0","-INF");header(2,"-18.0","0.0");header(3,"-24.0","-30.0");
 for(unsigned g=0;g<4;g++){
  unsigned base=20+55*g;
  for(unsigned y=60;y<176;y++)for(unsigned x=34;x<39;x++)assert(!fb[y*240+base+x]); /* 5 px meter/fader gap */
  for(unsigned ch=0;ch<2;ch++)for(unsigned x=18*ch;x<16+18*ch;x++)assert(fb[208*240+base+x]==swap16(gateway_meter_color(20))); /* 16 px bars */
  for(unsigned y=212;y<220;y++)for(unsigned x=0;x<34;x++)assert(!fb[y*240+base+x]);
  assert(fb[211*240+base+37]==swap16(RGB(255,212,40))); /* S bottom == meter bottom */
  for(unsigned y=212;y<220;y++)assert(!fb[y*240+base+37]);
  memcpy(saved,fb,sizeof saved);draw_text_box(base,220,34,&FONT_T,gateway_sources[g==2?PLAT_MONITOR_OUT:g==3?PLAT_MONITOR_MASTER:g],C_WHITE,1);
  for(unsigned y=220;y<240;y++)assert(!memcmp(fb+y*240+base,saved+y*240+base,34*sizeof(uint16_t))); /* names, no numbers/K labels */
 }
 scale();
 /* Updating either value must retain its neighbour; their clearing regions overlap. */
 gateway_meter[0].held=gateway_meter[1].held=400;gateway_mixer_draw(0);header(0,"-20.0","-12.0");
 gateway_mix_db[0]=-599;gateway_mixer_draw(0);header(0,"-20.0","-59.9");
 gateway_mix_db[0]=-600;gateway_mix_db[1]=-999;gateway_mix_db[2]=-1000;master=1;gateway_mixer_draw(1);
 header(0,"-20.0","-60.0");header(1,"-12.0","-99.9");header(2,"-18.0","-100");header(3,"-24.0","-161");
 const unsigned positions[]={0,410,2048,3072,4096};
 const char *const labels[]={"-INF","-60.0","-30.0","-15.0","0.0"};
 for(unsigned i=0;i<5;i++){master=positions[i];gateway_mixer_draw(0);header(3,"-24.0",labels[i]);scale();}
 assert(!cv_cuts && !cpu_work_depth);
 puts("PASS: 16-pixel stereo bars, shared Home-1 dB scale at left retained on incremental edits, 5-pixel fader gap, exact meter/fader-centred peak/gain text without units, bottom names without numbers, neighbouring labels retained on incremental edits");
}
