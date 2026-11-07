/* SPDX-License-Identifier: MPL-2.0
 * Foreground dB targets; only audio owns ramp histories. No allocation/locks in DSP. */
static int16_t gateway_mix_db[3];
static volatile float gateway_mix_target[3];
typedef struct { float gain,seen,step;uint32_t remaining,used; } gateway_fader_t;
static gateway_fader_t gateway_fader[3];
static uint8_t gateway_mixer_redraw;
/* Bits 0..2 mute USB/BT/OUT; MASTER shares gateway_muted and PLAY. */
static uint8_t gateway_mix_mutes,gateway_mix_solos,gateway_fade_time=3,gateway_fade_draft=3;
static const uint16_t gateway_fade_ms[]={0,10,50,100,500,1000};
static const char *const gateway_fade_names[]={"OFF","10ms","50ms","100ms","500ms","1s"};
static const uint8_t gateway_mute_keys[]={0,2,4,6},gateway_solo_keys[]={12,14,16,18};
static uint32_t gateway_keys;
#include "audio_gate.h"
typedef struct {uint32_t gain,rem,config,step,fraction,used;} gateway_gate_t;
static gateway_gate_t gateway_gate[3];
/* Atomic publication: effective cuts in low bits, fade frames above bit 7. */
static volatile uint32_t gateway_gate_config;
static unsigned gateway_mixer_muted(unsigned source){return source==3?gateway_muted:(gateway_mix_mutes>>source)&1u;}
static unsigned gateway_mixer_cut(unsigned source){
    unsigned group=source<2?3u:12u,solos=gateway_mix_solos&group;
    return gateway_mixer_muted(source) || (solos && !(solos&(1u<<source)));
}
static void gateway_mixer_publish(void){
    unsigned cuts=0;for(unsigned i=0;i<3;i++)cuts|=gateway_mixer_cut(i)<<i;
    uint32_t lock=plat_lock();
    gateway_gate_config=(gateway_fade_ms[gateway_fade_time]*441u/10u)<<8|cuts;
    plat_master_mute_fade_ms(gateway_fade_ms[gateway_fade_time]);
    plat_master_mute(gateway_mixer_cut(3));plat_unlock(lock);
    gateway_mixer_redraw=1;
}
static void gateway_gate_prepare(unsigned source){
    gateway_gate_t *g=&gateway_gate[source];uint32_t config=gateway_gate_config;
    if(config!=g->config){
        if((config>>8)!=(g->config>>8))g->rem=0;
        uint32_t frames=config>>8;
        g->step=frames?65536u/frames:0;g->fraction=frames?65536u%frames:0;g->config=config;
        if(!frames){g->gain=config&(1u<<source)?0:65536u;g->rem=0;}
    }
}
static uint32_t gateway_gate_next(unsigned source){
    gateway_gate_prepare(source);gateway_gate_t *g=&gateway_gate[source];g->used++;
    g->gain=audio_gate_tick(g->gain,g->config&(1u<<source)?0:65536u,&g->rem,g->config>>8,g->step,g->fraction);
    return g->gain;
}
static void gateway_mixer_toggle(unsigned source,int solo){
    if(solo)gateway_mix_solos^=1u<<source;
    else if(source==3)gateway_muted=!gateway_muted;
    else gateway_mix_mutes^=1u<<source;
    gateway_mixer_publish();gateway_lights();gateway_settings_changed();
}
static volatile uint8_t gateway_mix_meter_enabled;
static volatile uint32_t gateway_mix_peak[3][2]; /* USB/BT/OUT, before own mute/solo */
float app_input_fader_meter_gain(unsigned source){return gateway_fader[source].gain;}
int app_input_fader_meter_enabled(void){return gateway_mix_meter_enabled;}
void app_input_fader_meter_peaks(unsigned source,uint32_t l,uint32_t r){
    if(l>gateway_mix_peak[source][0])gateway_mix_peak[source][0]=l;
    if(r>gateway_mix_peak[source][1])gateway_mix_peak[source][1]=r;
}
static void gateway_mixer_observe(int enabled){
    uint32_t lock=plat_lock();gateway_mix_meter_enabled=0;
    for(unsigned s=0;s<3;s++)for(unsigned ch=0;ch<2;ch++)gateway_mix_peak[s][ch]=0;
    gateway_mix_meter_enabled=enabled!=0;plat_unlock(lock);
}
static float gateway_mix_linear(int db){return db<=GATEWAY_GAIN_OFF?0.0f:fm_expf((float)db*0.01151292546f);}
static void gateway_mixer_init(void){
    gateway_mixer_observe(0);
    gateway_mixer_publish();
    for(unsigned i=0;i<3;i++){
        float g=gateway_mix_linear(gateway_mix_db[i]);gateway_mix_target[i]=g;
        gateway_fader[i]=(gateway_fader_t){g,g,0,0,0};
        gateway_gate[i]=(gateway_gate_t){gateway_mixer_cut(i)?0:65536u,0,~gateway_gate_config,0,0,0};gateway_gate_prepare(i);
    }
}
static void gateway_fader_prepare(unsigned source){
    gateway_fader_t *f=&gateway_fader[source];float target=gateway_mix_target[source];
    if(target!=f->seen){f->seen=target;f->remaining=441;f->step=(target-f->gain)*(1.0f/441.0f);}
}
float app_input_fader_gain(unsigned source){
    gateway_fader_t *f=&gateway_fader[source];gateway_fader_prepare(source);f->used++;
    if(f->remaining){f->gain+=f->step;if(!--f->remaining)f->gain=f->seen;}
    uint32_t gate=gateway_gate_next(source);
    return gate==65536u?f->gain:f->gain*((float)gate*(1.0f/65536.0f));
}
static void gateway_fader_measure(uint32_t peak[2],const int32_t *lr,uint32_t n,float gain,unsigned source){
    for(unsigned i=0;i<2*n;i++){
        int32_t v=gain==1.0f?lr[i]:(int32_t)((float)lr[i]*gain);
        /* OUT shows what this trimmed signal would send without its gate,
         * including the final 24-bit saturation, without changing its PCM. */
        if(source==2){if(v>8388607)v=8388607;if(v< -8388608)v=-8388608;}
        uint32_t a=v<0?0u-(uint32_t)v:(uint32_t)v;
        if(a>peak[i&1])peak[i&1]=a;
    }
}
void app_input_fader_process(unsigned source,int32_t *lr,uint32_t n){
    gateway_fader_t *f=&gateway_fader[source];gateway_fader_prepare(source);
    gateway_gate_prepare(source);gateway_gate_t *gate=&gateway_gate[source];
    uint32_t target=gate->config&(1u<<source)?0:65536u;
    int meter=gateway_mix_meter_enabled;uint32_t peak[2]={0,0};
    if(!f->remaining && gate->gain==target){
        f->used+=n;
        gate->used+=n;float gain=target?f->gain:0.0f;
        if(meter && !target)gateway_fader_measure(peak,lr,n,f->gain,source);
        if(gain!=1.0f)for(unsigned i=0;i<2*n;i++)lr[i]=(int32_t)((float)lr[i]*gain);
        if(meter && target)gateway_fader_measure(peak,lr,n,1.0f,source);
    }else for(unsigned i=0;i<n;i++){
        float g=app_input_fader_gain(source);
        if(meter)gateway_fader_measure(peak,lr+2*i,1,f->gain,source);
        lr[2*i]=(int32_t)((float)lr[2*i]*g);lr[2*i+1]=(int32_t)((float)lr[2*i+1]*g);
    }
    if(meter)app_input_fader_meter_peaks(source,peak[0],peak[1]);
}
void app_input_fader_idle(unsigned source,uint32_t n){
    gateway_fader_t *f=&gateway_fader[source];gateway_fader_prepare(source);
    uint32_t missing=f->used<n?n-f->used:0;
    if(missing>=f->remaining){f->gain=f->seen;f->remaining=0;}
    else{f->gain+=f->step*(float)missing;f->remaining-=missing;}
    f->used=0;
    gateway_gate_prepare(source);gateway_gate_t *g=&gateway_gate[source];
    missing=g->used<n?n-g->used:0;
    g->gain=audio_gate_skip(g->gain,g->config&(1u<<source)?0:65536u,&g->rem,g->config>>8,g->step,g->fraction,missing);g->used=0;
}
static void gateway_mixer_idle(uint32_t n){
    for(unsigned i=0;i<2;i++)app_input_fader_idle(i,n);
}
void app_master_process(int32_t *lr,uint32_t n,float gain){
    gateway_mixer_idle(n);
    gateway_fx.capture=gateway_usb_audio_mode==GATEWAY_USB_MASTER_FX?gateway_usb_audio:0;
    fx_process(&gateway_fx,lr,n,gain);gateway_fx.capture=0;
    if(gateway_usb_source_fx(gateway_usb_audio_mode)){
        gateway_usb_fx->capture=gateway_usb_audio;
        fx_process(gateway_usb_fx,gateway_usb_audio,n,4096);gateway_usb_fx->capture=0;
    }
}
static void gateway_mixer_turn(unsigned enc,int delta){
    if(enc!=EN_K1 && enc!=EN_K2 && enc!=EN_K3)return;
    unsigned source=enc-EN_K1;
    int step=gateway_fx_step(enc,delta);delta=gateway_fx_clamp(delta,-2000,2000);
    int db=gateway_fx_clamp(gateway_mix_db[source]+step*delta,GATEWAY_GAIN_OFF,0);
    if(db==gateway_mix_db[source])return;
    gateway_mix_db[source]=db;
    float g=gateway_mix_linear(db);uint32_t lock=plat_lock();gateway_mix_target[source]=g;plat_unlock(lock);
    gateway_settings_changed();gateway_mixer_redraw=1;
}
