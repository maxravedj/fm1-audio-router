/* SPDX-License-Identifier: MPL-2.0
 * Focused target-path prepared PCM consumer; decoder/FIR remain unchanged. */
#define APP_INPUT_FADER_PRE_GATE_METER
static float app_input_fader_meter_gain(unsigned source);
#define main full_pipeline_main
#include "../../../tests/bt_resample_cpu2.c"
#undef main
static float meter_trim;
static unsigned meter_queries;
static float app_input_fader_meter_gain(unsigned source){assert(!core && source==1);meter_queries++;return meter_trim;}
int main(void){
    const float trims[]={1.0f,.5f,.00316227766f},gates[]={0.0f,.33f,1.0f};
    static int32_t audio[512],prepared[512];
    for(unsigned rate=44100;rate<=48000;rate+=3900)for(unsigned trim=0;trim<3;trim++)for(unsigned gate=0;gate<3;gate++)
    for(unsigned mode=1;mode<=2;mode++)for(unsigned gain=0;gain<=4096;gain+=1024)for(unsigned enabled=0;enabled<2;enabled++){
        reset(rate);btr_worker_enabled=0;btr_out_r=UINT32_MAX-127u;btr_out_w=btr_out_r+256;btr_primed=1;
        for(unsigned i=0;i<256;i++)for(unsigned ch=0;ch<2;ch++){
            int32_t v=(int32_t)(i*397+ch*31)-50000; /* exceeds int16; retain FIR headroom */
            prepared[2*i+ch]=btr_output[(btr_out_r+i)&(BTR_OUT_RING-1)][ch]=v;
        }
        record_mode=mode;meter_on=enabled;meter_trim=trims[trim];fader=meter_trim*gates[gate];
        meter_peak[0]=meter_peak[1]=meter_queries=0;memset(audio,0,sizeof audio);
        unsigned before=waits;irq=1;btr_pcm_mix(audio,256,gain);irq=0;assert(waits==before);
        uint32_t peaks[2]={0,0};
        for(unsigned i=0;i<512;i++){
            int32_t raw=(int32_t)((int64_t)prepared[i]*gain/16);
            int32_t post=(int32_t)((float)raw*fader),metered=(int32_t)((float)raw*meter_trim);
            uint32_t magnitude=metered<0?0u-(uint32_t)metered:(uint32_t)metered;
            if(magnitude>peaks[i&1])peaks[i&1]=magnitude;
            assert(audio[i]==post && monitored[i]==prepared[i]*256 && recorded[i]==(mode==1?raw:post));
        }
        assert(meter_queries==(enabled?256:0));
        assert(meter_peak[0]==(enabled?peaks[0]:0) && meter_peak[1]==(enabled?peaks[1]:0));
    }
    reset(44100);assert(!locks && !plat_cpu2_busy());
    puts("PASS: CPU2 target-path BT IN pregate trim peaks vs independent oracle, wrap/stereo/rates/levels/gates/raw-post routes; unchanged full-precision PCM, hidden meter callback skipped, no IRQ wait");
}
