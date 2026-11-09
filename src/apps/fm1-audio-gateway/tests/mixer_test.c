/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void turn(unsigned e,int d){enc_acc[e]=d;app_input_only();}
static void ui(void){
 now_ms=1000;app_init();app_ui_init();gateway_mode=GATEWAY_MODE_FULL;gateway_view_changed();assert(gateway_display_block);
 tap(B_GLO);app_loop();assert(gateway_screen==GATEWAY_MIXER && !gateway_display_block && !usb_input_wave_enabled && !usb_spectrum_enabled);
 assert(!usb_input_meter_enabled && gateway_mix_meter_enabled && master_meter_enabled && lights_b&(1u<<B_GLO));
 unsigned source=gateway_monitor;turn(EN_SELECT,1);turn(EN_K3,-20);assert(gateway_monitor==source && !gateway_mix_db[0] && !gateway_mix_db[1] && gateway_mix_db[2]<0);
 now_ms+=200;turn(EN_K1,-1);assert(gateway_mix_db[0]==-1 && !gateway_mix_db[1]);
 for(unsigned i=0;i<12;i++){now_ms+=15;turn(EN_K1,-1);}int db=gateway_mix_db[0];now_ms+=15;turn(EN_K1,-1);assert(gateway_mix_db[0]==db-30);
 now_ms+=200;db=gateway_mix_db[0];turn(EN_K1,1);assert(gateway_mix_db[0]==db+1);
 turn(EN_K2,INT_MIN);assert(gateway_mix_db[1]==GATEWAY_GAIN_OFF);turn(EN_K2,INT_MAX);assert(!gateway_mix_db[1]);
 int32_t raw[]={4194304,-2097152};usb_input_meter_samples(raw,1);app_input_fader_process(0,raw,1);app_input_fader_meter_peaks(1,2097152,1048576);master_meter_samples(raw,1);
 now_ms+=48;app_loop();assert(gateway_meter[0].level==gateway_meter_level(raw[0]) && gateway_meter[4].level>470 && gateway_meter[2].level>530);
 assert(gateway_mixer_peak_key[0] && gateway_mixer_peak_key[1]);
 master=0;now_ms+=48;app_loop();uint32_t bottom=gateway_mixer_fader_key[3];master=4096;now_ms+=48;app_loop();assert(gateway_mixer_fader_key[3]!=bottom);
 now_ms+=750;app_loop();gateway_settings_t s;assert(sizeof s==24 && plat_store_load(OBJ_SETTINGS,&s,sizeof s)==24 && s.version==16 && s.input_db[0]==gateway_mix_db[0]);
 int saved=gateway_mix_db[0];app_init();app_ui_init();assert(gateway_mix_db[0]==saved && gateway_fader[0].gain==gateway_mix_target[0]);
 for(unsigned bad=0;bad<3;bad++){s=gateway_settings();s.input_db[bad]=bad?1:GATEWAY_GAIN_OFF-1;plat_store_save(OBJ_SETTINGS,&s,sizeof s);app_init();assert(!gateway_mix_db[0] && !gateway_mix_db[1]);}
 for(unsigned v=8;v<=9;v++){s=gateway_settings();s.version=v;s.bt_reserved[0]=0;s.spectrum_range=3;s.input_db[0]=-300;s.reserved=v==9?3:1;
  plat_store_save(OBJ_SETTINGS,&s,16);app_init();assert(!gateway_mix_db[0] && !gateway_mix_db[1] && gateway_cpu_mode==(v==8));}
 app_ui_init();tap(B_GLO);tap(B_HOME);assert(gateway_screen==GATEWAY_MAIN && !gateway_mix_meter_enabled && !gateway_mix_peak[0][0] && !gateway_mix_peak[1][0]);
 puts("PASS: GLO mixer navigation/light, freed graphs, post-fader input/output meters, independent accelerated K1/K2/K3, MASTER pot follower, v15/24-byte save/reboot and v8/v9 migration/validation");
}
static void ramp(void){
 gateway_mix_db[0]=gateway_mix_db[1]=0;gateway_mixer_init();gateway_mixer_observe(1);gateway_mix_target[0]=0;
 int32_t lr[2*512];for(unsigned i=0;i<1024;i++)lr[i]=1000000;
 app_input_fader_process(0,lr,512);for(unsigned i=1;i<512;i++)assert(lr[2*i]<=lr[2*i-2]);
 assert(gateway_mix_peak[0][0]==(uint32_t)lr[0] && gateway_mix_peak[0][1]==(uint32_t)lr[1]);
 assert(lr[0]<1000000 && lr[0]>990000 && !lr[2*440] && !lr[2*511]);
 gateway_mix_target[1]=0;int32_t silence[512]={0};app_master_process(silence,256,4096);app_master_process(silence,256,4096);
 assert(gateway_fader[1].gain==0 && !gateway_fader[1].remaining); /* inactive BT advances too */
 puts("PASS: bounded monotonic 441-frame input fade, exact zero, absent Bluetooth ramp completes before reconnect");
}
static int32_t usb[512],out[512];static int16_t bt[512];
static void render(int usbdb,int btdb,unsigned full,unsigned pot,int32_t result[2]){
 gateway_mix_db[0]=usbdb;gateway_mix_db[1]=btdb;gateway_mixer_init();gateway_mixer_observe(1);gateway_fx_init();
 plat_bt_receiver_on(1);btr_audio_reset(44100);for(unsigned i=0;i<24;i++)btr_pcm_put(bt,256);
 master=pot;uac_full=full;plat_master_mute_init(0);plat_usb_play_gain(4096);usb_play_buf=usb;usb_play_n=256;
 plat_usb_input_observe(1,0);plat_master_meter_enable(1);
 for(unsigned block=0;block<6;block++){usb_play_at=0;sim_render(out);}result[0]=out[510];result[1]=out[511];
 uint32_t p[2];plat_usb_input_peaks(p);assert(p[0]==1000000 && p[1]==500000);
 plat_bt_input_peaks(p);static uint32_t raw_bt[2];
 if(!raw_bt[0]){memcpy(raw_bt,p,sizeof raw_bt);assert(abs((int)p[0]-4096000)<6000);}
 assert(!memcmp(raw_bt,p,sizeof raw_bt)); /* identical raw peaks for every trim; FIR DC ripple allowed */
 for(unsigned ch=0;ch<2;ch++){
  uint32_t usb_raw=ch?500000:1000000;
  assert(gateway_mix_peak[0][ch]==(uint32_t)((float)usb_raw*gateway_mix_linear(usbdb)));
  uint32_t bt_ref=(uint32_t)((float)raw_bt[ch]*gateway_mix_linear(btdb));
  assert(abs((int)gateway_mix_peak[1][ch]-(int)bt_ref)<=1);
 }
 gateway_mixer_observe(0);usb_play_at=0;sim_render(out);
 assert(!gateway_mix_peak[0][0] && !gateway_mix_peak[1][0]); /* hidden producer stops */
}
static void audio(void){
 for(unsigned i=0;i<256;i++){usb[2*i]=1000000;usb[2*i+1]=-500000;bt[2*i]=16000;bt[2*i+1]=-8000;}
 int32_t baseusb[2],basebt[2],sum[2],v[2];render(0,GATEWAY_GAIN_OFF,0,4096,baseusb);render(GATEWAY_GAIN_OFF,0,0,4096,basebt);
 for(unsigned full=0;full<2;full++){
  render(0,0,full,4096,sum);for(unsigned ch=0;ch<2;ch++)assert(abs(sum[ch]-baseusb[ch]-basebt[ch])<=2);
  render(-60,-120,full,4096,v);for(unsigned ch=0;ch<2;ch++){
   double ref=baseusb[ch]*pow(10,-6./20)+basebt[ch]*pow(10,-12./20);assert(fabs(v[ch]-ref)<4);
  }
  render(-60,-120,full,2048,v);double gain=(double)APP_MASTER_FX_GAIN(2048)/4096;
  for(unsigned ch=0;ch<2;ch++){double ref=(baseusb[ch]*pow(10,-6./20)+basebt[ch]*pow(10,-12./20))*gain;assert(fabs(v[ch]-ref)<4);}
  render(GATEWAY_GAIN_OFF,GATEWAY_GAIN_OFF,full,4096,v);assert(!v[0] && !v[1]);
 }
 usb_play_buf=0;plat_bt_receiver_on(0);
 puts("PASS: real USB+BT mixer pre-sum independent trims vs double dB oracle, stereo polarity, unity and exact silence, both full modes, MASTER once, exact post-fader stereo peaks, raw input peaks unaffected, hidden capture stopped");
}
int main(void){palette_set(APP_PALETTE);ui();ramp();audio();assert(!cv_cuts && !cpu_work_depth);}
