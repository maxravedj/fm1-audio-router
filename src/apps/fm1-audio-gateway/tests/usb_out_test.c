/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void turn(unsigned e,int d){enc_acc[e]=d;app_input_only();}
static void settings(void){
 now_ms=1000;app_init();app_ui_init();assert(!gateway_usb_out_mode && !gateway_usb_pcm);
 uint32_t free=plat_work_available();
 for(unsigned mode=0;mode<=6;mode++){
  tap(B_EDIT);turn(EN_SELECT,INT_MIN);turn(EN_SELECT,GATEWAY_SET_USB_OUT);assert(gateway_settings_row==GATEWAY_SET_USB_OUT);
  turn(EN_ALGO,INT_MIN);turn(EN_ALGO,mode);assert(gateway_usb_out_draft==mode);
  unsigned applied=gateway_usb_out_mode;uint32_t before=plat_work_available();
  store_fail=1;tap(B_SAVE);assert(gateway_save_notice==2 && gateway_usb_out_mode==applied && plat_work_available()==before);
  store_fail=0;tap(B_SAVE);assert(gateway_save_notice==1 && gateway_usb_out_mode==mode);
  assert(free-plat_work_available()==((gateway_usb_out_bytes(mode)+7u)&~7u));
  gateway_bar_key=~0u;gateway_draw_cpu();unsigned bx=3+text_w(&FONT_T,"USB IN")+3+2+text_w(&FONT_T,"BT")+3+2+text_w(&FONT_T,"FX")+3+2;
  assert(fb[2*240+bx]==swap16(mode?RGB(20,150,50):C_GRAY));
  assert(3+text_w(&FONT_T,"USB IN")+3+2+text_w(&FONT_T,"BT AAC")+3+2+text_w(&FONT_T,"FX")+3+2+text_w(&FONT_T,"USB OUT")+3<237-text_w(&FONT_T,"CPU 999% BAT 100%"));
  for(unsigned view=0;view<3;view++){
   gateway_mode=view;tap(B_HOME);assert(view!=2 || gateway_has_spectrum());assert(view!=1 || gateway_has_wave());
   tap(B_EDIT);assert(!gateway_display_block);
  }
  int32_t *p=gateway_usb_pcm;gateway_usb_session=mode;tap(B_SAVE);assert(gateway_usb_pcm==p && gateway_usb_session==mode); /* no unrelated-save reset */
  gateway_settings_t s;assert(plat_store_load(OBJ_SETTINGS,&s,sizeof s)==24 && s.version==17 && s.reserved>>2==mode);
  app_init();app_ui_init();assert(gateway_usb_out_mode==mode && gateway_usb_out_draft==mode);
  tap(B_EDIT);turn(EN_SELECT,INT_MIN);turn(EN_SELECT,GATEWAY_SET_USB_OUT);turn(EN_ALGO,INT_MAX);tap(B_HOME);tap(B_EDIT);assert(gateway_usb_out_draft==mode);
 }
 gateway_settings_t s=gateway_settings();s.version=10;s.bt_reserved[0]=0;s.spectrum_range=3;s.reserved=3;s.input_db[0]=-60;s.input_db[1]=-120;
 plat_store_save(OBJ_SETTINGS,&s,20);app_init();app_ui_init();assert(!gateway_usb_out_mode && gateway_mix_db[0]==-60 && gateway_mix_db[1]==-120 && !gateway_cpu_mode);
 for(unsigned bad=28;bad<256;bad++){
  s=gateway_settings();s.reserved=bad;s.zoom=9;plat_store_save(OBJ_SETTINGS,&s,sizeof s);app_init();assert(!gateway_usb_out_mode && gateway_zoom==6);
 }
 app_ui_init();tap(B_EDIT);turn(EN_SELECT,INT_MIN);turn(EN_SELECT,GATEWAY_SET_USB_OUT);turn(EN_ALGO,2);
 void *blocks[8];unsigned used=0;while(used<8 && (blocks[used]=plat_work_alloc(8)))used++;
 unsigned writes=store_writes;tap(B_SAVE);assert(gateway_save_notice==2 && !gateway_usb_out_mode && store_writes==writes);
 while(used)plat_work_free(blocks[--used]);
 puts("PASS: seven USB-out drafts/SAVE/failure/discard/reboot, 24-byte v15 packed mode and strict reserved validation, v10 trim migration, workspace sizing/release/allocation failure, unrelated saves preserve DSP session");
}
static int32_t usb[512],speaker[512];static int16_t bt[512];
static int out_db;
static int32_t recorded[512],baseline[512];static fx_params_t params;
static void start(unsigned mode,int usb_db,int bt_db,unsigned pot,int muted){
 gateway_usb_out_apply(0,0);gateway_fx_init();gateway_fx_publish(&params);
 fx_bank_t bank;assert(fx_build(&params,&bank));fx_init(&gateway_fx,&bank);
 gateway_usb_out_apply(gateway_usb_out_alloc(mode),mode);assert(!mode || gateway_usb_pcm);
 gateway_mix_db[0]=usb_db;gateway_mix_db[1]=bt_db;gateway_mix_db[2]=out_db;gateway_mixer_init();
 plat_bt_receiver_on(1);btr_audio_reset(44100);for(unsigned i=0;i<24;i++)btr_pcm_put(bt,256);
 master=pot;plat_master_mute_init(muted || gateway_mixer_cut(3));plat_usb_play_gain(4096);usb_play_buf=usb;usb_play_n=256;
}
static void render(unsigned mode,int ud,int bd,unsigned pot,int mute){
 start(mode,ud,bd,pot,mute);usb_rec_f=tmpfile();assert(usb_rec_f);usb_rec_frames=0;
 for(unsigned block=0;block<6;block++){usb_play_at=0;sim_render(speaker);} /* actual shared mixer and recording sink */
 assert(usb_rec_frames==6*256);fseek(usb_rec_f,5*256*4,SEEK_SET);
 for(unsigned i=0;i<512;i++){int lo=fgetc(usb_rec_f),hi=fgetc(usb_rec_f);assert(lo>=0 && hi>=0);recorded[i]=(int16_t)(lo|(hi<<8));}
 fclose(usb_rec_f);usb_rec_f=0;
}
static void audio(void){
 for(unsigned i=0;i<256;i++){usb[2*i]=1000000;usb[2*i+1]=-500000;bt[2*i]=16000;bt[2*i+1]=-8000;}
 fx_defaults(&params);
 /* Raw inputs ignore both faders, MASTER and PLAY; sum clips safely. */
 for(unsigned mode=1;mode<=6;mode++){
  render(mode,0,0,4096,0);memcpy(baseline,recorded,sizeof baseline);
  if(mode==1)assert(recorded[510]==3906 && recorded[511]==-1953);
  if(mode==3)assert(abs(recorded[510]-16000)<30 && abs(recorded[511]+8000)<20);
  if(mode==5)assert(abs(recorded[510]-19906)<30 && abs(recorded[511]+9953)<20);
  render(mode,0,0,0,1);assert(!memcmp(baseline,recorded,sizeof baseline));for(unsigned i=0;i<512;i++)assert(!speaker[i]);
  render(mode,-60,-120,2048,0);
  if(mode==1 || mode==3 || mode==5)assert(!memcmp(baseline,recorded,sizeof baseline));
  else {double g=mode==2?pow(10,-6./20):mode==4?pow(10,-12./20):0;
   if(g)for(unsigned i=0;i<512;i++)assert(fabs(recorded[i]-baseline[i]*g)<2);
   else assert(abs(recorded[510])<abs(baseline[510]));
  }
 }
 render(0,0,0,4096,0);for(unsigned i=0;i<512;i++)assert(!recorded[i]);
 /* Speaker path is bit-identical for every recording route; source FX do not
  * receive the other source or share detector/filter history with main FX. */
 params.chain=1;params.post=60;params.ceiling=-60;params.band[2].gain=90;params.band[2].q=150;
 render(6,-60,-120,4096,0);memcpy(baseline,recorded,sizeof baseline);
 for(unsigned i=0;i<512;i++)assert(abs(recorded[i]-((speaker[i]+128)>>8))<=1);
 render(6,-60,-120,0,1);assert(!memcmp(baseline,recorded,sizeof baseline));
 render(0,-60,-120,2048,0);memcpy(baseline,speaker,sizeof baseline);
 for(unsigned mode=1;mode<=6;mode++){render(mode,-60,-120,2048,0);assert(!memcmp(baseline,speaker,sizeof baseline));}
 for(unsigned mode=2;mode<=4;mode+=2){
  render(mode,-60,-120,4096,0);memcpy(baseline,recorded,sizeof baseline);
  if(mode==2)for(unsigned i=0;i<512;i++)bt[i]=0;else for(unsigned i=0;i<512;i++)usb[i]=0;
  render(mode,-60,-120,0,1);assert(!memcmp(baseline,recorded,sizeof baseline));
  for(unsigned i=0;i<256;i++){usb[2*i]=1000000;usb[2*i+1]=-500000;bt[2*i]=16000;bt[2*i+1]=-8000;}
 }
 /* Raw combined can exceed FS; output remains saturated, never wraps. */
 for(unsigned i=0;i<256;i++){usb[2*i]=8000000;usb[2*i+1]=-8000000;bt[2*i]=30000;bt[2*i+1]=-30000;}
 render(5,GATEWAY_GAIN_OFF,GATEWAY_GAIN_OFF,0,1);assert(recorded[510]==32767 && recorded[511]==-32768);
 fx_defaults(&params);render(6,0,0,2048,0); /* -30 dB at the custom MASTER midpoint. */
 assert(recorded[510]==32767 && recorded[511]==-32768);
 assert(speaker[510]>300000 && speaker[510]<1000000 && speaker[511]<-300000); /* capture clamp preserves speaker headroom */
 /* No host recording: second FX is idle and retains its counter. Reopening
  * starts a fresh source-only detector/delay rather than replaying old audio. */
 start(4,0,0,4096,0);usb_play_at=0;sim_render(speaker);assert(gateway_usb_fx->time==0);
 app_usb_out_begin(256,1);assert(gateway_usb_audio_mode==4);app_usb_out_end(256);
 gateway_usb_fx->time=123;app_usb_out_begin(256,0);assert(!gateway_usb_audio_mode);app_usb_out_begin(256,1);assert(!gateway_usb_fx->time);app_usb_out_end(256);
 start(4,0,0,4096,0);usb_rec_f=tmpfile();assert(usb_rec_f);
 for(unsigned i=0;i<2;i++){usb_play_at=0;sim_render(speaker);}
 fx_params_t edit=params;edit.chain=1;edit.post=-60;gateway_fx_publish(&edit);
 assert(gateway_usb_fx->pending.post==gateway_fx.pending.post && gateway_usb_fx->generation==1);
 for(unsigned i=0;i<2;i++){usb_play_at=0;sim_render(speaker);}
 assert(gateway_usb_fx->seen==1 && !gateway_usb_fx->fade);
 fclose(usb_rec_f);usb_rec_f=0;
 usb_play_buf=0;plat_bt_receiver_on(0);gateway_usb_out_apply(0,0);
 puts("PASS: real USB recording all seven routes, raw fader bypass, post-FX trim, pre-MASTER/pre-mute, isolated source-only EQ/limiter, identical speaker output, saturation, idle CPU gate and fresh capture restart");
}
static void output_monitor(void){
 /* Third fader trims every record route, independently of speaker/master/mute. */
 fx_defaults(&params);
 for(unsigned i=0;i<256;i++){usb[2*i]=1000000;usb[2*i+1]=-500000;bt[2*i]=16000;bt[2*i+1]=-8000;}
 for(unsigned mode=1;mode<=6;mode++){
  out_db=0;render(mode,0,0,4096,0);memcpy(baseline,recorded,sizeof baseline);
  int32_t reference[512];memcpy(reference,speaker,sizeof reference);
  out_db=-60;render(mode,0,0,4096,0);
  for(unsigned i=0;i<512;i++)assert(fabs(recorded[i]-baseline[i]*pow(10,-6./20))<2);
  assert(!memcmp(reference,speaker,sizeof reference));
  out_db=GATEWAY_GAIN_OFF;render(mode,0,0,0,1);for(unsigned i=0;i<512;i++)assert(!recorded[i] && !speaker[i]);
 }
 /* Attenuation precedes USB clipping, including the pre-MASTER FX capture. */
 for(unsigned i=0;i<256;i++){usb[2*i]=8000000;usb[2*i+1]=-8000000;bt[2*i]=30000;bt[2*i+1]=-30000;}
 out_db=-60;
 for(unsigned mode=5;mode<=6;mode++){
  render(mode,0,0,4096,0);double ref=(8000000.+30000.*256)*pow(10,-6./20)/256.;
  assert(fabs(recorded[510]-ref)<40 && fabs(recorded[511]+ref)<40 && recorded[510]>30000);
 }
 /* K3 saves/restores with the old input trims; v11 defaults OUT to unity. */
 gateway_screen=GATEWAY_MIXER;gateway_view_changed();now_ms+=200;turn(EN_K3,-1);
 int trim=gateway_mix_db[2];assert(trim==-61);now_ms+=750;app_loop();
 gateway_settings_t saved=gateway_settings();assert(saved.version==17 && sizeof saved==24 && saved.input_db[2]==trim);
 app_init();app_ui_init();assert(gateway_mix_db[2]==trim);
 saved.version=11;saved.bt_reserved[0]=0;saved.spectrum_range=3;saved.input_db[0]=-60;saved.input_db[1]=-120;saved.bt_reserved[1]=PLAT_MONITOR_MASTER;
 plat_store_save(OBJ_SETTINGS,&saved,20);app_init();app_ui_init();
 assert(gateway_mix_db[0]==-60 && gateway_mix_db[1]==-120 && !gateway_mix_db[2]);
 gateway_mode=GATEWAY_MODE_FULL;gateway_monitor=PLAT_MONITOR_OUT;gateway_view_changed();
 /* Accepted OUT monitoring does not run source FX until the host records. */
 out_db=-60;start(2,0,0,0,1);gateway_out_observe(1);
 uint32_t at=usb_wave_count;usb_play_at=0;sim_render(speaker);
 assert(usb_wave_count==at+256 && !gateway_usb_fx->time);
 int16_t point[4];assert(usb_wave_read(usb_wave_count-1,0,point) && !point[0] && !point[2]);
 uint32_t peaks[2];gateway_out_peaks(peaks);assert(!peaks[0] && !peaks[1]);
 for(unsigned i=0;i<256;i++){usb[2*i]=1000000;usb[2*i+1]=-500000;}
 usb_rec_f=tmpfile();assert(usb_rec_f);
 for(unsigned i=0;i<4;i++){usb_play_at=0;sim_render(speaker);}
 assert(gateway_usb_fx->time==1024 && usb_wave_read(usb_wave_count-1,0,point));
 assert(abs(point[0]-1957)<3 && abs(point[2]+978)<3);
 gateway_out_peaks(peaks);assert(peaks[0]>500000 && peaks[0]<502000 && peaks[1]>250000 && peaks[1]<251000);
 fclose(usb_rec_f);usb_rec_f=0;
 gateway_view_changed();usb_play_at=0;sim_render(speaker);now_ms+=48;app_frame();assert(!gateway_meter[0].level);
 assert(usb_wave_read(usb_wave_count-1,0,point) && !point[0] && !point[2]);
 /* No stale sample/peak on source changes; OUT gets the same real FFT path. */
 gateway_mode=GATEWAY_MODE_SPECTRUM;gateway_view_changed();assert(gateway_has_spectrum());
 usb_rec_f=tmpfile();assert(usb_rec_f);
 for(unsigned block=0;block<PLAT_SPECTRUM_FRAMES/256+2;block++){
  for(unsigned i=0;i<256;i++){int32_t tone=(int32_t)(4194304.0*sin(6.283185307179586*1000*(i+block*256)/44100.0));usb[2*i]=tone;usb[2*i+1]=-tone;}
  usb_play_at=0;sim_render(speaker);
 }
 now_ms+=48;app_loop();assert(gateway_spectrum_peak[22]>-18000 && gateway_spectrum_peak[22]<-9000);
 fclose(usb_rec_f);usb_rec_f=0;
 turn(EN_SELECT,2);assert(gateway_monitor==PLAT_MONITOR_USB && !usb_spectrum_filled);
 turn(EN_SELECT,-2);assert(gateway_monitor==PLAT_MONITOR_OUT && !usb_spectrum_filled);
 gateway_screen=GATEWAY_SETTINGS;gateway_view_changed();assert(!gateway_out_visible && !gateway_display_block);
 gateway_out_peaks(peaks);usb_play_at=0;sim_render(speaker);gateway_out_peaks(peaks);assert(!peaks[0] && !peaks[1]);
 /* OFF/absent OUT advances its ramp on the audio clock as well. */
 gateway_mix_target[2]=0;sim_render(speaker);sim_render(speaker);assert(!gateway_fader[2].remaining && !gateway_fader[2].gain);
 gateway_usb_out_apply(0,0);usb_play_buf=0;plat_bt_receiver_on(0);out_db=0;
 puts("PASS: OUT six-route trim/pre-clip headroom/exact silence, unchanged speaker, K3 persistence and v11 migration, post-fader meter/waveform/FFT, idle silence/FX gate, source cycling/reset, hidden capture release and absent-output ramp");
}
static void latency(void){
 /* Actual source-only and shared post-FX recording must follow the same tap
  * policy as speaker PCM, independently of physical MASTER and PLAY mute. */
 memset(usb,0,sizeof usb);memset(bt,0,sizeof bt);usb[0]=1000000;usb[1]=-500000;
 out_db=0;
 for(unsigned mode=2;mode<=6;mode+=4)for(unsigned state=0;state<3;state++){
  fx_defaults(&params);params.eq=0;params.chain=state!=0;params.lim=state==2;
  start(mode,0,0,0,1);usb_rec_f=tmpfile();assert(usb_rec_f);usb_rec_frames=0;
  usb_play_at=0;sim_render(speaker);
  unsigned at=state==2?FX_DELAY:0;
  for(unsigned i=0;i<256;i++){
   assert(gateway_usb_pcm[2*i]==(i==at?1000000:0));
   assert(gateway_usb_pcm[2*i+1]==(i==at?-500000:0));
   assert(!speaker[2*i] && !speaker[2*i+1]);
  }
  fclose(usb_rec_f);usb_rec_f=0;
 }
 gateway_usb_out_apply(0,0);usb_play_buf=0;plat_bt_receiver_on(0);
 puts("PASS: actual USB-only/shared post-FX impulse timing: zero delay in global/limiter bypass, exactly 128 frames enabled, recording remains pre-MASTER/pre-mute");
}
static void mute_solo_routes(void){
 static int32_t expected_speaker[512],expected_record[512],raw_record[512];
 for(unsigned i=0;i<256;i++){usb[2*i]=1000000;usb[2*i+1]=-500000;bt[2*i]=16000;bt[2*i+1]=-8000;}
 out_db=0;fx_defaults(&params);params.chain=1;params.eq=1;params.lim=1;
 params.band[0].type=FX_BELL;params.band[0].hz=1000;params.band[0].gain=60;
 gateway_fade_time=0;
 for(unsigned mode=1;mode<=6;mode++){
  gateway_mix_mutes=gateway_mix_solos=gateway_muted=0;
  render(mode,0,0,4096,0);memcpy(raw_record,recorded,sizeof raw_record);
  for(unsigned inputs=0;inputs<4;inputs++){
   gateway_mix_mutes=gateway_mix_solos=0;
   render(mode,inputs&1?GATEWAY_GAIN_OFF:0,inputs&2?GATEWAY_GAIN_OFF:0,4096,0);
   memcpy(expected_speaker,speaker,sizeof speaker);
   memcpy(expected_record,(mode&1)?raw_record:recorded,sizeof recorded);
   /* Muting an input must be equivalent to OFF only on post-trim routes. */
   gateway_mix_mutes=inputs;render(mode,0,0,4096,0);
   assert(!memcmp(speaker,expected_speaker,sizeof speaker));assert(!memcmp(recorded,expected_record,sizeof recorded));
   /* Input solos implement the same cut without disturbing output grouping. */
   gateway_mix_mutes=0;gateway_mix_solos=inputs==1?2:inputs==2?1:0;
   if(inputs<3){render(mode,0,0,4096,0);assert(!memcmp(speaker,expected_speaker,sizeof speaker));assert(!memcmp(recorded,expected_record,sizeof recorded));}
  }
  gateway_mix_mutes=gateway_mix_solos=0;render(mode,0,0,4096,0);
  memcpy(expected_speaker,speaker,sizeof speaker);memcpy(expected_record,recorded,sizeof recorded);
  for(unsigned state=0;state<5;state++){
   gateway_mix_mutes=state==0?4:0;gateway_muted=state==1;gateway_mix_solos=state==2?4:state==3?8:state==4?12:0;
   render(mode,0,0,4096,0);
   int out_cut=state==0 || state==3,master_cut=state==1 || state==2;
   if(out_cut){for(unsigned i=0;i<512;i++)assert(!recorded[i]);}
   else assert(!memcmp(recorded,expected_record,sizeof recorded));
   if(master_cut){for(unsigned i=0;i<512;i++)assert(!speaker[i]);}
   else assert(!memcmp(speaker,expected_speaker,sizeof speaker));
  }
  gateway_muted=gateway_mix_mutes=gateway_mix_solos=0;
 }
 gateway_usb_out_apply(0,0);usb_play_buf=0;plat_bt_receiver_on(0);gateway_fade_time=3;gateway_mixer_init();
 puts("PASS: six real USB recording routes with input mute/solo vs independent OFF reference, raw taps preserved, source-only/shared FX, OUT versus MASTER mute/solo isolation and multi-solo");
}
int main(void){palette_set(APP_PALETTE);settings();audio();output_monitor();latency();mute_solo_routes();assert(!cv_cuts && !cpu_work_depth);}
