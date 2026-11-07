/* SPDX-License-Identifier: MPL-2.0
 * Main owns workspace/publication; audio alone owns PCM/DSP session histories. */
enum { GATEWAY_USB_OFF,GATEWAY_USB_LOOP,GATEWAY_USB_LOOP_FX,GATEWAY_USB_BT,
    GATEWAY_USB_BT_FX,GATEWAY_USB_MASTER,GATEWAY_USB_MASTER_FX };
static const char *const gateway_usb_out_names[]={"OFF","USB IN","USB IN post FX","BT IN","BT IN post FX","Master","Master post FX"};
static volatile uint8_t gateway_usb_out_mode;
static uint8_t gateway_usb_out_draft,gateway_usb_audio_mode,gateway_usb_session;
static int32_t *volatile gateway_usb_pcm;
static int32_t *gateway_usb_audio;
static fx_engine_t *volatile gateway_usb_fx;
static volatile uint8_t gateway_out_visible;
static volatile uint32_t gateway_out_peak[2];
static void gateway_out_observe(int visible){
    uint32_t lock=plat_lock();gateway_out_visible=visible;
    gateway_out_peak[0]=gateway_out_peak[1]=0;plat_unlock(lock);
}
static void gateway_out_peaks(uint32_t lr[2]){
    uint32_t lock=plat_lock();lr[0]=gateway_out_peak[0];lr[1]=gateway_out_peak[1];
    gateway_out_peak[0]=gateway_out_peak[1]=0;plat_unlock(lock);
}
static int gateway_usb_source_fx(unsigned mode){return mode==GATEWAY_USB_LOOP_FX || mode==GATEWAY_USB_BT_FX;}
static uint32_t gateway_usb_out_bytes(unsigned mode){
    return mode?2u*APP_BLOCK*sizeof(int32_t)+(gateway_usb_source_fx(mode)?sizeof(fx_engine_t):0):0;
}
static int32_t *gateway_usb_out_alloc(unsigned mode){
    if(!mode)return 0;
    /* A route can outlive AAC's large low-end decoder allocations. Keep it
     * high, beside the SBC queue, so releasing AAC restores one graph gap. */
    int32_t *p=plat_work_alloc_top(gateway_usb_out_bytes(mode));if(!p)return 0;
    if(gateway_usb_source_fx(mode))fx_init((fx_engine_t *)(p+2u*APP_BLOCK),&gateway_fx.pending);
    return p;
}
static void gateway_usb_out_apply(int32_t *p,unsigned mode){
    if(p==gateway_usb_pcm && mode==gateway_usb_out_mode)return;
    int32_t *old=gateway_usb_pcm;
    if(old && p && old!=p){
        /* SAVE prepared a replacement while the old route kept recording.
         * Reclaim both blocks before publishing, avoiding a hole that could
         * prevent the large spectrum allocation on returning HOME. */
        uint32_t lock=plat_lock();gateway_usb_pcm=gateway_usb_audio=0;gateway_usb_fx=0;
        gateway_usb_out_mode=gateway_usb_audio_mode=gateway_usb_session=0;plat_unlock(lock);
        plat_work_free(old);plat_work_free(p);old=0;p=gateway_usb_out_alloc(mode);
    }
    if(!p)mode=0;
    uint32_t lock=plat_lock();
    gateway_usb_audio=0;gateway_usb_audio_mode=gateway_usb_session=0;
    gateway_usb_pcm=p;gateway_usb_out_mode=mode;
    gateway_usb_fx=gateway_usb_source_fx(mode)?(fx_engine_t *)(p+2u*APP_BLOCK):0;
    plat_unlock(lock);if(old!=p)plat_work_free(old);
}
/* Called with the same short lock as the main chain's coefficient publication. */
static void gateway_usb_out_publish(const fx_bank_t *b){
    if(gateway_usb_fx){gateway_usb_fx->pending=*b;gateway_usb_fx->generation++;}
}
void app_usb_out_begin(uint32_t n,int capturing){
    unsigned mode=capturing && gateway_usb_pcm && n<=APP_BLOCK?gateway_usb_out_mode:0;
    if(mode!=gateway_usb_session && gateway_usb_source_fx(mode))fx_init(gateway_usb_fx,&gateway_fx.pending);
    gateway_usb_session=mode;gateway_usb_audio_mode=mode;gateway_usb_audio=mode?gateway_usb_pcm:0;
    if(mode)memset(gateway_usb_audio,0,2u*n*sizeof(int32_t));
}
void app_usb_out_usb(const int32_t *lr,uint32_t n,int post){
    unsigned mode=gateway_usb_audio_mode;
    if(post?mode==GATEWAY_USB_LOOP_FX:(mode==GATEWAY_USB_LOOP || mode==GATEWAY_USB_MASTER))
        memcpy(gateway_usb_audio,lr,2u*n*sizeof(int32_t));
}
unsigned app_usb_out_bt_mode(void){
    unsigned mode=gateway_usb_audio_mode;
    return mode==GATEWAY_USB_BT_FX?2u:(mode==GATEWAY_USB_BT || mode==GATEWAY_USB_MASTER)?1u:0u;
}
void app_usb_out_bt_sample(unsigned frame,unsigned ch,int32_t sample){
    unsigned i=2u*frame+ch;
    if(gateway_usb_audio_mode==GATEWAY_USB_MASTER)gateway_usb_audio[i]+=sample;
    else gateway_usb_audio[i]=sample;
}
const int32_t *app_usb_out_end(uint32_t n){
    int32_t *p=gateway_usb_audio;
    if(p)app_input_fader_process(2,p,n);
    if(p)for(unsigned i=0;i<2u*n;i++){
        if(p[i]>8388607)p[i]=8388607;if(p[i]<-8388608)p[i]=-8388608;
    }
    app_input_fader_idle(2,n);
    if(gateway_out_visible){
        if(p)for(unsigned i=0;i<2u*n;i++){
            uint32_t a=p[i]<0?0u-(uint32_t)p[i]:(uint32_t)p[i];
            if(a>gateway_out_peak[i&1])gateway_out_peak[i&1]=a;
        }
        plat_audio_monitor_out_samples(p,n);
    }
    gateway_usb_audio=0;gateway_usb_audio_mode=0;return p;
}
