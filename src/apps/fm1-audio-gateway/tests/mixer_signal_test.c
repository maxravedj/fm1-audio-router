/* SPDX-License-Identifier: MPL-2.0
 * Real trim/gate PCM, pregate meters, Home PLAY redraw and FX page recall. */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void clear_peaks(void){memset((void *)gateway_mix_peak,0,sizeof gateway_mix_peak);}
static uint32_t magnitude(int32_t v){return v<0?0u-(uint32_t)v:(uint32_t)v;}
static void direct(void){
    static int32_t lr[512];
    for(unsigned source=0;source<=2;source+=2)for(unsigned trim=0;trim<2;trim++)for(unsigned fade=0;fade<=5;fade+=fade?2:3){
        gateway_mix_mutes=gateway_mix_solos=gateway_muted=0;gateway_fade_time=fade;
        gateway_mix_db[source]=trim?-60:0;gateway_mixer_init();gateway_mixer_observe(1);
        gateway_mix_mutes=1u<<source;gateway_mixer_publish();
        int32_t l=source==2?16000000:4194304,r=source==2?-16000000:-2097152;
        float gain=gateway_mix_linear(gateway_mix_db[source]);
        int32_t ml=(int32_t)((float)l*gain),mr=(int32_t)((float)r*gain);
        if(source==2){if(ml>8388607)ml=8388607;if(mr< -8388608)mr=-8388608;}
        unsigned blocks=gateway_fade_ms[fade]*441u/10u/256u+2u;
        for(unsigned b=0;b<blocks;b++){
            for(unsigned i=0;i<256;i++){lr[2*i]=l;lr[2*i+1]=r;}
            clear_peaks();app_input_fader_process(source,lr,256);app_input_fader_idle(source,256);
            assert(gateway_mix_peak[source][0]==magnitude(ml) && gateway_mix_peak[source][1]==magnitude(mr));
        }
        for(unsigned i=0;i<512;i++)assert(!lr[i]); /* meter stays live after the fade completes */
    }
    gateway_fade_time=0;gateway_mix_mutes=1;gateway_mix_db[0]=0;gateway_mixer_init();gateway_mixer_observe(1);
    gateway_mix_target[0]=.5f;
    for(unsigned i=0;i<512;i++)lr[i]=1000000;
    clear_peaks();app_input_fader_process(0,lr,256);app_input_fader_idle(0,256);
    assert(abs((int)gateway_mix_peak[0][0]-998866)<3); /* independent 441-frame trim ramp */
    for(unsigned i=0;i<512;i++)assert(!lr[i]);
    app_input_fader_process(0,lr,256);app_input_fader_idle(0,256);assert(gateway_fader[0].gain==.5f);
    for(unsigned k=0;k<2;k++){
        for(unsigned i=0;i<512;i++)lr[i]=k?200000:2000000;
        clear_peaks();app_input_fader_process(0,lr,256);app_input_fader_idle(0,256);
        assert(gateway_mix_peak[0][0]==(k?100000:1000000));
    }
    gateway_mixer_observe(0);for(unsigned i=0;i<512;i++)lr[i]=1000000;
    app_input_fader_process(0,lr,256);assert(!gateway_mix_peak[0][0] && !gateway_mix_peak[2][0]);
    puts("PASS: USB IN/USB OUT meters remain live after mute at OFF/100ms/1s, follow trim and changing signal, match post-trim/clipped reference, hidden taps stop");
}
static int32_t usb[512],out[512];static int16_t bt[512];
static void render(unsigned mute,unsigned solo,uint32_t peaks[3][2],uint32_t master_peak[2]){
    gateway_fade_time=0;gateway_mix_mutes=mute&7;gateway_muted=!!(mute&8);gateway_mix_solos=solo;
    gateway_mix_db[0]=-60;gateway_mix_db[1]=-120;gateway_mix_db[2]=-60;gateway_mixer_init();gateway_mixer_observe(1);
    gateway_out_observe(0);gateway_usb_out_apply(0,0);gateway_usb_out_apply(gateway_usb_out_alloc(GATEWAY_USB_LOOP),GATEWAY_USB_LOOP);
    gateway_fx_init();plat_bt_receiver_on(1);btr_audio_reset(44100);for(unsigned k=0;k<24;k++)btr_pcm_put(bt,256);
    plat_master_meter_enable(1);plat_master_peaks(master_peak);plat_master_mute_init(gateway_mixer_cut(3));master=4096;
    usb_play_buf=usb;usb_play_n=256;plat_usb_play_gain(4096);usb_rec_f=tmpfile();assert(usb_rec_f);
    for(unsigned k=0;k<6;k++){usb_play_at=0;sim_render(out);}
    memcpy(peaks,(const void *)gateway_mix_peak,sizeof gateway_mix_peak);plat_master_peaks(master_peak);
    fclose(usb_rec_f);usb_rec_f=0;
}
static void audio(void){
    for(unsigned i=0;i<256;i++){usb[2*i]=1000000;usb[2*i+1]=-500000;bt[2*i]=16000;bt[2*i+1]=-8000;}
    uint32_t base[3][2],got[3][2],master_base[2],master_peak[2];render(0,0,base,master_base);
    for(unsigned i=0;i<3;i++)assert(base[i][0]>100000 && base[i][1]>50000);
    for(unsigned kind=0;kind<2;kind++)for(unsigned strip=0;strip<4;strip++){
        render(kind?0:1u<<strip,kind?1u<<strip:0,got,master_peak);
        assert(!memcmp(base,got,sizeof base)); /* all three meters see identical ungated, trimmed PCM */
        if(gateway_mixer_cut(3)){assert(!memcmp(master_base,master_peak,sizeof master_peak));for(unsigned i=0;i<512;i++)assert(!out[i]);}
        if(gateway_mixer_cut(2))for(unsigned i=0;i<512;i++)assert(!gateway_usb_pcm[i]);
        gateway_meter_ms=now_ms;for(unsigned i=0;i<3;i++)for(unsigned ch=0;ch<2;ch++){
            unsigned pair=i==0?0:i==1?4:6;gateway_meter_step(&gateway_meter[pair+ch],got[i][ch],48);
        }
        gateway_mixer_draw(1);
        for(unsigned i=0;i<3;i++){
            unsigned pair=i==0?0:i==1?4:6;assert(gateway_meter[pair].level>0);
            assert(fb[208*240+20+55*i]==swap16(gateway_mixer_cut(i)?RGB(90,90,90):gateway_meter_color(20)));
        }
    }
    /* HOME's OUT tap still represents the real gated send. */
    gateway_screen=GATEWAY_MAIN;gateway_mode=GATEWAY_MODE_METERS;gateway_view_changed();assert(!gateway_mix_meter_enabled && gateway_out_visible);
    usb_rec_f=tmpfile();assert(usb_rec_f);usb_play_at=0;sim_render(out);fclose(usb_rec_f);usb_rec_f=0;
    uint32_t send[2];gateway_out_peaks(send);assert(!send[0] && !send[1]);
    for(unsigned i=0;i<3;i++)assert(!gateway_mix_peak[i][0] && !gateway_mix_peak[i][1]);
    gateway_usb_out_apply(0,0);usb_play_buf=0;plat_bt_receiver_on(0);
    puts("PASS: actual BT IN/USB IN/USB OUT pregate stereo peaks remain identical across every individual mute/solo; real sends stay gated, MASTER stays premute, HOME OUT stays postgate");
}
static uint16_t screen[240*240];
static void play_redraw(void){
    gateway_muted=gateway_mix_mutes=gateway_mix_solos=0;gateway_mixer_init();
    gateway_screen=GATEWAY_MAIN;gateway_mode=GATEWAY_MODE_METERS;gateway_view_changed();app_loop();
    for(unsigned i=0;i<8;i++)gateway_meter[i]=(gateway_meter_t){480,480,750,0,0,0};
    gateway_draw_meter(1);gateway_meter_chrome_redraw=0;gateway_meter_ms=gateway_cpu_ms=now_ms;
    memcpy(screen,fb,sizeof screen);uint32_t changes=lcd_changes,draws=blits;
    for(unsigned k=0;k<2;k++){
        tap(B_PLAY);assert(!gateway_meter_chrome_redraw);app_loop();
        assert(lcd_changes==changes && blits==draws && !memcmp(screen,fb,sizeof screen));
    }
    puts("PASS: HOME 1 PLAY mute/unmute leaves unchanged meter pixels intact with zero LCD clears/blits");
}
static void fx_recall(void){
    gateway_screen=GATEWAY_MAIN;gateway_fx_limiter=0;gateway_view_changed();
    tap(B_FX);assert(!gateway_fx_limiter);tap(B_FX);assert(gateway_fx_limiter);
    const unsigned pages[]={B_HOME,B_EDIT,B_GLO};
    for(unsigned fx=0;fx<2;fx++){
        if(!fx){tap(B_FX);assert(!gateway_fx_limiter);}else{tap(B_FX);assert(gateway_fx_limiter);}
        for(unsigned p=0;p<3;p++){
            unsigned band=gateway_fx_row;tap(pages[p]);assert(gateway_screen!=GATEWAY_FX && gateway_fx_limiter==fx);
            tap(B_FX);assert(gateway_screen==GATEWAY_FX && gateway_fx_limiter==fx && gateway_fx_row==band);
        }
    }
    assert(!strcmp(gateway_sources[0],"USB IN") && !strcmp(gateway_sources[1],"BT IN") && !strcmp(gateway_sources[3],"USB OUT"));
    assert(!strcmp(gateway_setting_names[GATEWAY_SET_USB_OUT],"USB OUT:") && !strcmp(gateway_usb_out_names[1],"USB IN") && !strcmp(gateway_usb_out_names[3],"BT IN"));
    for(unsigned i=0;i<4;i++)assert(text_w(&FONT_T,gateway_sources[i])<=34);
    puts("PASS: FX reopens remembered EQ/limiter from HOME/Settings/Mixer and preserves band, in-FX tap alternates; UI source labels are USB IN/BT IN/USB OUT");
}
int main(void){
    setbuf(stdout,NULL);palette_set(APP_PALETTE);now_ms=1000;app_init();app_ui_init();gateway_screen=GATEWAY_MIXER;gateway_view_changed();
    direct();audio();play_redraw();fx_recall();assert(!cv_cuts && !cpu_work_depth);
}
