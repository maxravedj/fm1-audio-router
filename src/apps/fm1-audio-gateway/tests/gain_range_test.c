/* SPDX-License-Identifier: MPL-2.0
 * Actual app/mixer: quiet gain resolution, OFF semantics and saved migrations. */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void turn(unsigned e,int d){now_ms+=200;enc_acc[e]=d;app_input_only();}
/* Independent double Hermite-basis oracle (production uses generated float
 * Horner coefficients). Tangents are the PCHIP solution for the five points. */
static double master_reference(unsigned p){
 static const double x[]={0,.1,.5,.75,1},y[]={0,.001,.031622776601683793,.17782794100389228,1};
 static const double m[]={0,.015331575416080944,.1438968078744184,.9930493873500345,4.640622025172229};
 if(!p)return 0;if(p>=4096)return 1;
 double position=p/4096.0;unsigned i=position<.1?0:position<.5?1:position<.75?2:3;
 double h=x[i+1]-x[i],t=(position-x[i])/h,t2=t*t,t3=t2*t;
 return (2*t3-3*t2+1)*y[i]+(t3-2*t2+t)*h*m[i]+(-2*t3+3*t2)*y[i+1]+(t3-t2)*h*m[i+1];
}
static void curve(void){
 float previous=0;unsigned distinct=0;
 assert(!APP_MASTER_FX_GAIN(0) && APP_MASTER_FX_GAIN(4096)==4096 && APP_MASTER_FX_GAIN(UINT_MAX)==4096);
 for(unsigned p=1;p<=4096;p++){
  double reference=master_reference(p);
  float actual=APP_MASTER_FX_GAIN(p)/4096.0f;
  assert(actual>0 && actual>previous && fabs(actual/reference-1)<0.000002);
  assert(abs(gateway_master_db(p)-(int)(200*log10(reference)-.5))<=1);
  distinct+=actual!=previous;previous=actual;
 }
 assert(distinct==4096 && gateway_master_db(410)==-600 && gateway_master_db(2048)==-300 && gateway_master_db(3072)==-150);
 assert(gateway_master_db(1)<GATEWAY_GAIN_OFF); /* A finite level, not the OFF sentinel. */
 double ratio=APP_MASTER_FX_GAIN(2)/APP_MASTER_FX_GAIN(1);assert(ratio>3.9 && ratio<4.1); /* Smooth zero slope at silence. */
 const unsigned joins[]={410,2048,3072};
 for(unsigned i=0;i<3;i++){
  unsigned p=joins[i];double left=APP_MASTER_FX_GAIN(p)-APP_MASTER_FX_GAIN(p-1),right=APP_MASTER_FX_GAIN(p+1)-APP_MASTER_FX_GAIN(p);
  assert(left>0 && right>0 && fabs(left/right-1)<.02);
 }
 for(int db=GATEWAY_GAIN_MIN;db<=0;db++){
  double ref=pow(10.0,db/200.0);float actual=gateway_mix_linear(db);
  assert(actual>0 && fabs(actual/ref-1)<0.0001);
 }
 assert(!gateway_mix_linear(GATEWAY_GAIN_OFF) && gateway_mix_linear(-600)>0);
 puts("PASS: custom MASTER anchors 0% silence / 10% -60 / 50% -30 / 75% -15 / 100% unity, all 4096 positions vs independent double oracle, continuous slopes, matching dB readout, unchanged faders/OFF");
}
static void audio(void){
 static int32_t input[512],output[512];const unsigned positions[]={0,1,16,128,409,410,411,512,1024,1536,2048,3072,4096};
 for(unsigned i=0;i<256;i++){input[2*i]=8388607;input[2*i+1]=-4194304;}
 gateway_mix_db[0]=gateway_mix_db[1]=0;gateway_mixer_init();gateway_fx_init();
 plat_bt_receiver_on(0);plat_usb_play_gain(4096);plat_master_meter_enable(1);plat_master_mute_init(0);
 usb_play_buf=input;usb_play_n=256;
 for(unsigned full=0;full<2;full++)for(unsigned k=0;k<sizeof positions/sizeof positions[0];k++){
  master=positions[k];uac_full=full;usb_play_at=0;uint32_t peak[2];plat_master_peaks(peak);sim_render(output);plat_master_peaks(peak);
  double g=master_reference(master);
  for(unsigned ch=0;ch<2;ch++){
   double ref=input[ch]*g;assert(fabs(output[ch]-ref)<=fabs(ref)*0.000002+1);
   int32_t exact=(int32_t)((float)input[ch]*(APP_MASTER_FX_GAIN(master)/4096.0f));
   assert(output[ch]==exact && peak[ch]==(uint32_t)abs(exact));
  }
  if(master==1)assert(!output[0] && APP_MASTER_FX_GAIN(master)>0); /* Below one 24-bit PCM LSB. */
  if(master==16)assert(output[0]>0 && output[0]<100);
 }
 /* Sub--60-dB input faders really attenuate PCM, without altering polarity. */
 const int values[]={-599,-600,-601,-750,-900,-999,GATEWAY_GAIN_MIN,GATEWAY_GAIN_OFF};
 for(unsigned source=0;source<3;source++)for(unsigned k=0;k<sizeof values/sizeof values[0];k++){
  gateway_mix_db[source]=values[k];gateway_mixer_init();int32_t pcm[2]={8388607,-4194304};
  app_input_fader_process(source,pcm,1);
  double g=values[k]==GATEWAY_GAIN_OFF?0:pow(10.0,values[k]/200.0);
  assert(fabs(pcm[0]-8388607*g)<2 && fabs(pcm[1]+4194304*g)<2);
 }
 /* OFF -> lowest finite value still uses the existing bounded 441-frame ramp. */
 gateway_mix_db[0]=GATEWAY_GAIN_OFF;gateway_mixer_init();gateway_mix_target[0]=gateway_mix_linear(GATEWAY_GAIN_MIN);
 int32_t before=0;
 for(unsigned i=0;i<512;i++){
  int32_t pcm[2]={8388607,-4194304};app_input_fader_process(0,pcm,1);
  assert(pcm[0]>=before && pcm[0]-before<=1 && pcm[0]<=84);before=pcm[0];
 }
 assert(before>=83 && !gateway_fader[0].remaining);
 usb_play_buf=0;
 puts("PASS: actual speaker MASTER once at all positions/both USB modes, post-MASTER peaks, audible PCM below -60 dB, all three faders and quiet OFF ramp");
}
static void persistence(void){
 store_len[OBJ_SETTINGS]=-1;app_init();app_ui_init();tap(B_GLO);
 for(unsigned e=EN_K1;e<=EN_K3;e++){
  turn(e,INT_MIN);assert(gateway_mix_db[e-EN_K1]==GATEWAY_GAIN_OFF);
  turn(e,1);assert(gateway_mix_db[e-EN_K1]==GATEWAY_GAIN_MIN && gateway_mix_target[e-EN_K1]>0);
  gateway_mix_db[e-EN_K1]=-600;gateway_mixer_init();turn(e,-1);
  assert(gateway_mix_db[e-EN_K1]==-601 && gateway_mix_target[e-EN_K1]>0);
 }
 now_ms+=750;app_loop();gateway_settings_t s;assert(plat_store_load(OBJ_SETTINGS,&s,sizeof s)==24 && s.version==16);
 app_init();app_ui_init();for(unsigned i=0;i<3;i++)assert(gateway_mix_db[i]==-601);
 for(unsigned v=10;v<=13;v++){
  s=gateway_settings();s.version=v;s.bt_reserved[0]=0;s.spectrum_range=v>=13?90:3;
  s.input_db[0]=-600;s.input_db[1]=-599;s.input_db[2]=-600;
  assert(!plat_store_save(OBJ_SETTINGS,&s,v>=12?22:20));app_init();app_ui_init();
  assert(gateway_mix_db[0]==GATEWAY_GAIN_OFF && gateway_mix_db[1]==-599 && gateway_mix_db[2]==(v>=12?GATEWAY_GAIN_OFF:0));
 }
 s=gateway_settings();s.input_db[0]=-600;s.input_db[1]=-1000;s.input_db[2]=GATEWAY_GAIN_OFF;
 assert(!plat_store_save(OBJ_SETTINGS,&s,sizeof s));app_init();app_ui_init();
 assert(gateway_mix_db[0]==-600 && gateway_mix_db[1]==-1000 && gateway_mix_db[2]==GATEWAY_GAIN_OFF);
 for(unsigned i=0;i<3;i++){
  s=gateway_settings();s.input_db[i]=GATEWAY_GAIN_OFF-1;
  assert(!plat_store_save(OBJ_SETTINGS,&s,sizeof s));app_init();for(unsigned j=0;j<3;j++)assert(!gateway_mix_db[j]);
 }
 s=gateway_settings();s.version=13;s.bt_reserved[0]=0;s.input_db[0]=-601;
 assert(!plat_store_save(OBJ_SETTINGS,&s,sizeof s));app_init();assert(!gateway_mix_db[0]);
 puts("PASS: K1/K2/K3 cross -60 and step between -100 and OFF; v16/24-byte save/reboot, v10-v13 OFF-preserving migration and version-aware bounds");
}
int main(void){setbuf(stdout,NULL);palette_set(APP_PALETTE);now_ms=100;app_init();app_ui_init();curve();audio();persistence();assert(!cv_cuts && !cpu_work_depth);}
