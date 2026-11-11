/* SPDX-License-Identifier: MPL-2.0 */
/* Gateway: CPU1 decodes/resamples, CPU0 consumes prepared PCM in ALNK0.
 * Legacy/host path below retains the original single-stage ring. */
#include "bt_receiver.h"
#include "../../include/ui_profile.h"
#include "bt_receiver_fir.h"
#ifdef APP_BT_RESAMPLE_CPU2
/* Repartition the old 32-KiB PCM allocation: 8 KiB decoded input + 16 KiB
 * full-precision FIR output. The freed 8 KiB also gives SBC+FFT scratch room. */
#define BTR_RING 2048u
#define BTR_TARGET 3072u
#define BTR_OUT_RING 2048u
#define BTR_RESAMPLE_BLOCK 256u
static int32_t btr_output[BTR_OUT_RING][2]; /* exact FIR sum >> 15; no int16 truncation */
static volatile uint32_t btr_out_w,btr_out_r;
static volatile uint8_t btr_worker_enabled=1;
static uint32_t btr_worker_started,btr_output_underruns;
static uint8_t btr_worker_outstanding;
static void btr_resample_fill(void);
static void btr_resample_poll(void);
static void btr_decode_poll_irq(void);
static int btr_worker_run(void (*fn)(uint32_t),uint32_t arg)
{
    uint32_t lock=plat_lock();
    int ok=0;
    if(btr_worker_enabled && !plat_cpu2_busy()) {
        btr_worker_started=plat_ticks24();
        ok=plat_cpu2_run(fn,arg);
        if(ok)btr_worker_outstanding=1;
    }
    plat_unlock(lock);return ok;
}
static void btr_worker_check(void)
{
    /* Validate completed requests too: busy() deliberately reports false on
     * a worker fault/rejected mailbox. Do not lose the platform crash guard.
     * IRQ exclusion prevents an audio kick between idle check and wait. */
    uint32_t lock=plat_lock();
    if(btr_worker_outstanding && !plat_cpu2_busy()) {
        plat_cpu2_wait();btr_worker_outstanding=0;
    }
    plat_unlock(lock);
    if(btr_worker_outstanding && plat_ticks24()-btr_worker_started>=2000000u*PLAT_TICKS_PER_US)
        plat_cpu2_wait(); /* exceptional timeout, foreground only */
}
static void btr_worker_stop(void)
{
    uint32_t lock=plat_lock();btr_worker_enabled=0;plat_unlock(lock);
    plat_cpu2_wait();plat_cpu2_sync();btr_worker_outstanding=0; /* main only: join before reset/free */
}
#else
#define BTR_RING 8192u
#define BTR_TARGET 6144u
#endif
/* aptX's stereo lane kernel loads each signed high-word pair as one word. */
static int16_t btr_pcm[BTR_RING][2] __attribute__((aligned(4)));
#if defined(APP_BT_APTX) && defined(APP_BT_RESAMPLE_CPU2)
/* Optional low byte, allocated with aptX/LDAC state. SBC/AAC retain exact 16-bit FIR arithmetic. */
static uint8_t (*btr_pcm_low)[2];
#include "aptx_dec/aptx_fir.h"
static uint32_t btr_aptx_decode_us,btr_aptx_decode_max,btr_aptx_decode_jobs;
static uint32_t btr_aptx_fir_us,btr_aptx_fir_max,btr_aptx_fir_blocks;
#endif
static volatile uint32_t btr_w,btr_r;
#ifdef APP_BT_RESAMPLE_CPU2
static uint32_t btr_pcm_space(void)
{
    plat_cpu2_sync();return BTR_RING-(btr_w-btr_r);
}
#endif
static uint32_t btr_phase,btr_rate=44100,btr_step=65536;
static uint8_t btr_primed;
static int32_t btr_servo;
static volatile struct btr_counters btr_stats; /* atomic 32-bit worker counters, visible to foreground diagnostics */
void btr_audio_reset(uint32_t rate)
{
#ifdef APP_AUDIO_MONITOR
    plat_audio_monitor_reset(1);
#endif
    btr_r=btr_w; btr_phase=0;btr_servo=0;btr_primed=0;btr_rate=rate;
    btr_step=rate==48000?71332:65536;
#ifdef APP_BT_RESAMPLE_CPU2
    btr_out_r=btr_out_w=0;btr_output_underruns=0;btr_worker_enabled=1;
#endif
}
static void btr_pcm_put(const int16_t *p,uint32_t frames)
{
    uint32_t i,w=btr_w,r=btr_r;
#if defined(APP_BT_AAC) || defined(APP_BT_SBC_CPU2)
    plat_cpu2_sync(); /* acquire slots released after the consumer's reads */
#endif
    if(frames>BTR_RING-(w-r)){btr_stats.overruns++;return;}
    for(i=0;i<frames;i++){btr_pcm[(w+i)&(BTR_RING-1)][0]=p[2*i];btr_pcm[(w+i)&(BTR_RING-1)][1]=p[2*i+1];}
#if defined(APP_BT_AAC) || defined(APP_BT_SBC_CPU2)
    plat_cpu2_sync();
#endif
    __asm__ volatile("" ::: "memory"); btr_w=w+frames;
#ifdef APP_BT_RESAMPLE_CPU2
    btr_resample_fill(); /* CPU1 only: prefer prepared output after each decoded AU/frame */
#endif
}
#ifdef APP_BT_RESAMPLE_CPU2
/* CPU1 owns raw r/w, phase and servo. CPU0 owns output r; CPU1 publishes w
 * after a complete block. No allocator, routing, gain or display calls here. */
static void btr_resample_fill(void)
{
    uint32_t r=btr_r,w=btr_w,ow=btr_out_w,base=btr_rate==48000?71332:65536;
    const int16_t (*filter)[96]=btr_rate==48000?BTR_FIR_48:BTR_FIR_44;
    for(;;) {
        uint32_t or=btr_out_r;plat_cpu2_sync();
        if(BTR_OUT_RING-(ow-or)<BTR_RESAMPLE_BLOCK || w-r<98u+279u)break;
        /* Include prepared output (in source-frame units) in the drift servo;
         * otherwise moving samples into this ring looks like lost input. */
        uint32_t avail=w-r+(((ow-or)*base)>>16);
        int32_t error=((int32_t)avail-(int32_t)BTR_TARGET)/16;
        if(error>66)error=66;if(error< -66)error= -66;
        btr_servo+=(error*256-btr_servo)/32;btr_step=base+btr_servo/256;
#ifdef APP_BT_APTX
        uint32_t fir_start=btr_pcm_low?plat_ticks24():0;
        if(btr_pcm_low){
            for(uint32_t i=0;i<BTR_RESAMPLE_BLOCK;i++){
                btr_fir24(btr_pcm,btr_pcm_low,r,filter[btr_phase>>10],btr_phase&1023u,btr_output[(ow+i)&(BTR_OUT_RING-1)]);
                btr_phase+=btr_step;r+=btr_phase>>16;btr_phase&=65535;
            }
            unsigned us=(plat_ticks24()-fir_start)/PLAT_TICKS_PER_US;
            btr_aptx_fir_us+=us;btr_aptx_fir_blocks++;if(us>btr_aptx_fir_max)btr_aptx_fir_max=us;
        }else
#endif
        for(uint32_t i=0;i<BTR_RESAMPLE_BLOCK;i++) {
            int64_t sum[2]={0,0};
            uint32_t phase=btr_phase>>10,frac=btr_phase&1023u,k,half;
        for (half=0;half<96;half+=48) {
            int32_t left=0,right=0;
            for (k=half;k<half+48;k++) {
                /* Exact signed /1024, truncated toward zero, without a divide
                 * per tap on pi32v2. Keep the original Q15 interpolation. */
                int32_t d=(filter[phase+1][k]-filter[phase][k])*(int32_t)frac+512;
                int32_t c=filter[phase][k]+((d+(d<0?1023:0))>>10);
                left+=(int32_t)btr_pcm[(r+k)&(BTR_RING-1)][0]*c;
                right+=(int32_t)btr_pcm[(r+k)&(BTR_RING-1)][1]*c;
            }
            /* Each 48-tap half has sum(abs(coeff)) <= 50531, including
             * interpolation rounding. Full-scale int16 PCM therefore fits
             * int32 exactly. Widen only twice per output, before combining.
             * The focused tests prove this bound against the actual tables. */
            sum[0]+=left;sum[1]+=right;
        }
            btr_output[(ow+i)&(BTR_OUT_RING-1)][0]=(int32_t)(sum[0]>>15);
            btr_output[(ow+i)&(BTR_OUT_RING-1)][1]=(int32_t)(sum[1]>>15);
            btr_phase+=btr_step;r+=btr_phase>>16;btr_phase&=65535;
        }
        btr_r=r;ow+=BTR_RESAMPLE_BLOCK;
        plat_cpu2_sync();btr_out_w=ow;
    }
}
static void btr_resample_job(uint32_t unused){(void)unused;btr_resample_fill();}
static void btr_resample_poll(void)
{
    uint32_t ow=btr_out_w,or=btr_out_r;plat_cpu2_sync();
    /* A decode already in flight fills output before returning. Otherwise
     * preparation has priority over starting the next bounded decode job. */
    if(BTR_OUT_RING-(ow-or)>=BTR_RESAMPLE_BLOCK && btr_w-btr_r>=377u)
        btr_worker_run(btr_resample_job,0);
}
void btr_pcm_mix(int32_t *lr,uint32_t n,uint32_t gain)
{
    uint32_t r=btr_out_r,w=btr_out_w;plat_cpu2_sync();
    if(!btr_primed){if(w-r<1536u){btr_resample_poll();btr_decode_poll_irq();return;}btr_primed=1;}
    plat_profile_span_t timing=plat_profile_begin();
#ifdef APP_USB_OUT_ROUTING
    unsigned record_mode=app_usb_out_bt_mode();
#endif
#ifdef APP_INPUT_FADERS
    int fader_meter=app_input_fader_meter_enabled();uint32_t fader_peak[2]={0,0};
#endif
#ifdef APP_AUDIO_MONITOR
    plat_audio_monitor_bt_begin();
#endif
    for(uint32_t i=0;i<n;i++) {
        if(r==w){btr_output_underruns++;btr_primed=0;break;}
        const int32_t *p=btr_output[r&(BTR_OUT_RING-1)];
#ifdef APP_AUDIO_MONITOR
        #ifdef APP_BT_APTX
        if(btr_pcm_low)plat_audio_monitor_bt_sample(p[0],p[1]);else
#endif
        plat_audio_monitor_bt_sample(p[0]*256,p[1]*256);
#endif
#ifdef APP_INPUT_FADERS
        float input_fader=app_input_fader_gain(1);
#ifdef APP_INPUT_FADER_PRE_GATE_METER
        float meter_fader=fader_meter?app_input_fader_meter_gain(1):1.0f;
#endif
#endif
        for(uint32_t ch=0;ch<2;ch++) {
            int32_t scaled;
#ifdef APP_BT_APTX
            if(btr_pcm_low)scaled=0;else
#endif
            scaled=p[ch]*(int32_t)gain;
            int32_t sample=(scaled+(scaled<0?15:0))>>4;
#ifdef APP_BT_APTX
            if(btr_pcm_low){int64_t full=(int64_t)p[ch]*gain;sample=(int32_t)((full+(full<0?4095:0))>>12);}
#endif
#ifdef APP_USB_OUT_ROUTING
            int32_t raw_sample=sample;
#endif
#ifdef APP_INPUT_FADERS
#ifdef APP_INPUT_FADER_PRE_GATE_METER
            int32_t before_fader=sample;
#endif
            if(input_fader!=1.0f)sample=(int32_t)((float)sample*input_fader);
            if(fader_meter){
#ifdef APP_INPUT_FADER_PRE_GATE_METER
                /* Reuse the audio multiplication whenever its gain equals
                 * the trim (the ordinary unmuted case). */
                int32_t v=input_fader==meter_fader?sample:meter_fader==1.0f?before_fader:(int32_t)((float)before_fader*meter_fader);
#else
                int32_t v=sample;
#endif
                uint32_t a=v<0?0u-(uint32_t)v:(uint32_t)v;
                if(a>fader_peak[ch])fader_peak[ch]=a;}
#endif
#ifdef APP_USB_OUT_ROUTING
            if(record_mode)app_usb_out_bt_sample(i,ch,record_mode==2?sample:raw_sample);
#endif
            lr[2*i+ch]+=sample;
        }
        r++;
    }
#ifdef APP_INPUT_FADERS
    if(fader_meter)app_input_fader_meter_peaks(1,fader_peak[0],fader_peak[1]);
#endif
    plat_cpu2_sync();btr_out_r=r;
    btr_resample_poll();btr_decode_poll_irq(); /* kicks only: never wait or allocate in audio */
    plat_profile_end(PROFILE_BT_MIX,timing);
}
#else
void btr_pcm_mix(int32_t *lr,uint32_t n,uint32_t gain)
{
    uint32_t i,k,ch,half,r=btr_r,w=btr_w,avail=w-r,base=btr_rate==48000?71332:65536;
#if defined(APP_BT_AAC) || defined(APP_BT_SBC_CPU2)
    plat_cpu2_sync(); /* acquire the published PCM before reading it */
#endif
    int32_t error;
#ifdef APP_USB_OUT_ROUTING
    unsigned record_mode=app_usb_out_bt_mode();
#endif
#ifdef APP_INPUT_FADERS
    int fader_meter=app_input_fader_meter_enabled();uint32_t fader_peak[2]={0,0};
#endif
    if(!btr_primed){if(avail<BTR_TARGET)return;btr_primed=1;}
#ifdef APP_AUDIO_MONITOR
    plat_audio_monitor_bt_begin();
#endif
    /* About 140 ms buffered at 44.1k: main-loop flash commits/LCD stalls
     * must not immediately starve Bluetooth while SRAM audio IRQs keep running.
     * Low-bandwidth occupancy servo, limited to ±1000 ppm (66 Q16 units). */
    error=((int32_t)avail-(int32_t)BTR_TARGET)/16;
    if (error > 66) error = 66;
    if (error < -66) error = -66;
    btr_servo+=(error*256-btr_servo)/32;
    btr_step=base+btr_servo/256;
    for(i=0;i<n;i++) {
        int64_t sum[2] = {0,0};
        const int16_t (*filter)[96] = btr_rate == 48000 ? BTR_FIR_48 : BTR_FIR_44;
        uint32_t phase = btr_phase >> 10, frac = btr_phase & 1023u;
        if (w-r < 98) { btr_stats.underruns++; btr_primed=0; btr_phase=0; break; }
        for (half=0;half<96;half+=48) {
            int32_t left=0,right=0;
            for (k=half;k<half+48;k++) {
                /* Exact signed /1024, truncated toward zero, without a divide
                 * per tap on pi32v2. Keep the original Q15 interpolation. */
                int32_t d=(filter[phase+1][k]-filter[phase][k])*(int32_t)frac+512;
                int32_t c=filter[phase][k]+((d+(d<0?1023:0))>>10);
                left+=(int32_t)btr_pcm[(r+k)&(BTR_RING-1)][0]*c;
                right+=(int32_t)btr_pcm[(r+k)&(BTR_RING-1)][1]*c;
            }
            /* Each 48-tap half has sum(abs(coeff)) <= 50531, including
             * interpolation rounding. Full-scale int16 PCM therefore fits
             * int32 exactly. Widen only twice per output, before combining.
             * The focused tests prove this bound against the actual tables. */
            sum[0]+=left;sum[1]+=right;
        }
#ifdef APP_AUDIO_MONITOR
        plat_audio_monitor_bt_sample((int32_t)(sum[0]>>15)*256,(int32_t)(sum[1]>>15)*256);
#endif
#ifdef APP_INPUT_FADERS
        float input_fader=app_input_fader_gain(1);
#ifdef APP_INPUT_FADER_PRE_GATE_METER
        float meter_fader=fader_meter?app_input_fader_meter_gain(1):1.0f;
#endif
#endif
        for (ch=0;ch<2;ch++) {
            /* gain is 0..4096. The full-filter coefficient norm < 101062
             * keeps this product within int32. Cancel 256/4096 to /16,
             * preserving the old shift-before-gain and signed truncation. */
            int32_t scaled=(int32_t)(sum[ch]>>15)*(int32_t)gain;
            int32_t sample=(scaled+(scaled<0?15:0))>>4;
#ifdef APP_USB_OUT_ROUTING
            int32_t raw_sample=sample;
#endif
#ifdef APP_INPUT_FADERS
#ifdef APP_INPUT_FADER_PRE_GATE_METER
            int32_t before_fader=sample;
#endif
            if(input_fader!=1.0f)sample=(int32_t)((float)sample*input_fader);
            if(fader_meter){
#ifdef APP_INPUT_FADER_PRE_GATE_METER
                /* Reuse the audio multiplication whenever its gain equals
                 * the trim (the ordinary unmuted case). */
                int32_t v=input_fader==meter_fader?sample:meter_fader==1.0f?before_fader:(int32_t)((float)before_fader*meter_fader);
#else
                int32_t v=sample;
#endif
                uint32_t a=v<0?0u-(uint32_t)v:(uint32_t)v;
                if(a>fader_peak[ch])fader_peak[ch]=a;}

#endif
#ifdef APP_USB_OUT_ROUTING
            if(record_mode)app_usb_out_bt_sample(i,ch,record_mode==2?sample:raw_sample);
#endif
            lr[2*i+ch]+=sample;
        }
        btr_phase+=btr_step;r+=btr_phase>>16;btr_phase&=65535;
    }
#ifdef APP_INPUT_FADERS
    if(fader_meter)app_input_fader_meter_peaks(1,fader_peak[0],fader_peak[1]);
#endif
#if defined(APP_BT_AAC) || defined(APP_BT_SBC_CPU2)
    plat_cpu2_sync();
#endif
    __asm__ volatile("" ::: "memory");btr_r=r;
}

#endif
