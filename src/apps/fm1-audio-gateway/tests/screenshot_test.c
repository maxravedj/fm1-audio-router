/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static uint16_t swap(uint16_t v){return (v>>8)|(v<<8);}
static void control(unsigned op){uint8_t p[]={0x7d,0x4c,0x49,op};hsx_q=0;assert(app_sysex(p,sizeof p));}
static void begin(void){
 control(2);hsx_q=0;gateway_screenshot_step();assert(gateway_shot_frozen && gateway_shot_ready);
}
static void request(unsigned y){uint8_t p[]={0x7d,0x4c,0x49,1,y&127,y>>7,y&127};hsx_q=0;assert(app_sysex(p,sizeof p));}
static int32_t pcm[512],out[512];
int main(void){
 app_init();app_ui_init();now_ms=1000;app_loop();app_frame();
 uint16_t px[]={0x00f8,0xe007,0x1f00,0xffff};
 plat_screenshot_begin(239);lcd_fill(239,238,9,9,0x1234);lcd_blit(237,238,2,2,px);plat_screenshot_end();
 assert(plat_screenshot_pixel(237)==0x001f && plat_screenshot_pixel(238)==0xffff && plat_screenshot_pixel(239)==0x1234);
 for(unsigned i=0;i<512;i++)pcm[i]=i&1?-524288:1048576;
 usb_play_buf=pcm;usb_play_n=256;plat_usb_play_gain(4096);gateway_fade_time=0;
 for(unsigned page=0;page<9;page++){
  control(0);master=4096;
  gateway_screen=page<3?GATEWAY_MAIN:page==3?GATEWAY_SETTINGS:page==4?GATEWAY_MIXER:GATEWAY_FX;
  gateway_mode=page<3?page:0;gateway_fx_limiter=page==6 || page==8;gateway_view_changed();if(page!=8){app_loop();app_frame();}
  for(unsigned i=0;i<8;i++)gateway_meter[i]=(gateway_meter_t){400+i*20,500+i*10,750,0,0,0};
  if(gateway_has_wave())for(unsigned x=0;x<GATEWAY_WAVE_W;x++){
   gateway_wave[x][0]=-10000-(x%30)*500;gateway_wave[x][1]=5000+(x%30)*500;
   gateway_wave[x][2]=-8000;gateway_wave[x][3]=8000;
  }
  if(gateway_has_spectrum())for(unsigned i=0;i<40;i++){
   gateway_spectrum_level[i]=-1000-(int)i*1000;gateway_spectrum_peak[i]=gateway_spectrum_level[i]+900;
  }
  if(page>=7)gateway_fx_dialog=page==7?1:2;
  gateway_settings_dirty=0;begin();static uint16_t expected[240*240];memcpy(expected,fb,sizeof fb);
  unsigned changes=lcd_changes,bl=blits,writes=store_writes;
  gateway_meter_t meters[8];memcpy(meters,gateway_meter,sizeof meters);
  uint32_t old_wave_position=usb_wave_count;
  for(unsigned y=0;y<240;y++){
   request(y);hsx_n=0;hsx_q=0;
   for(unsigned c=0;c<8;c++){
    master=y&1?0:4096;usb_play_at=0;sim_render(out);
    assert(out[300]==(master?1048576:0) && out[301]==(master?-524288:0));
    now_ms++;app_loop();app_frame();hsx_q=0;
   }
   assert(hsx_n==8*129 && !gateway_shot_pending && gateway_shot_frozen);
   for(unsigned c=0;c<8;c++){
    uint8_t *f=hsx_out+c*129;assert(f[0]==0xf0 && f[4]==(y&127) && f[7]==c && f[128]==0xf7);
    for(unsigned x=0;x<30;x++){
     uint8_t *b=f+8+x*4;uint16_t v=(b[0]<<12)|(b[1]<<8)|(b[2]<<4)|b[3];
     assert(v==swap(expected[y*240+c*30+x]));
    }
   }
  }
  assert(changes==lcd_changes && bl==blits && writes==store_writes && !memcmp(expected,fb,sizeof fb));
  assert(!memcmp(meters,gateway_meter,sizeof meters));
  if(gateway_has_wave())assert(usb_wave_count>old_wave_position);
  control(0);assert(!gateway_shot_frozen);now_ms+=50;app_loop();app_frame();assert(lcd_changes>changes);
 }
 gateway_screen=GATEWAY_MAIN;gateway_mode=GATEWAY_MODE_METERS;gateway_fx_dialog=0;gateway_view_changed();app_loop();app_frame();
 begin();request(0);hsx_q=HSX_RING;gateway_screenshot_step();assert(gateway_shot_pending==2 && !gateway_shot_chunk);
 now_ms+=5001;app_loop();assert(!gateway_shot_frozen && !gateway_shot_pending);
 begin();hsx_session++;app_loop();assert(!gateway_shot_frozen);
 begin();uint32_t stamp=gateway_shot_start;now_ms+=100;control(2);gateway_screenshot_step();assert(gateway_shot_start==stamp);
 for(unsigned i=0;i<8;i++){now_ms+=4000;control(2);gateway_screenshot_step();}
 assert(!gateway_shot_frozen); /* hard limit, despite keepalives */
 begin();held_btn=1u<<B_HOME;app_frame();assert(!gateway_shot_frozen);held_btn=0;app_input_only();
 request(0);assert(!gateway_shot_pending); /* interrupted transfer cannot silently restart */
 begin();enc_acc[EN_SELECT]=1;app_input_only();assert(!gateway_shot_frozen);
 begin();unsigned muted=gateway_muted;held_btn=1u<<B_PLAY;app_frame();assert(!gateway_shot_frozen && gateway_muted!=muted);held_btn=0;app_input_only();
 begin();app_bt_aac_prepare();assert(!gateway_shot_frozen); /* before graph buffers can be reused */
 begin();app_panic();assert(!gateway_shot_frozen && !gateway_shot_pending && !gateway_display_block);
 app_ui_init();begin();uint8_t invalid[]={0x7d,0x4c,0x49,1,127,127,1};app_sysex(invalid,sizeof invalid);assert(!gateway_shot_pending);control(0);
 puts("PASS: frozen RGB565 screenshot equals paused LCD on all 7 pages and both FX dialogs while live PCM/master/history continue; no display or settings writes during download");
 puts("PASS: resume on end, idle/hard timeout, USB reconnect, panel/encoder/PLAY changes, AAC buffer release and updater; idempotent begin, backpressure and invalid rows");
}
