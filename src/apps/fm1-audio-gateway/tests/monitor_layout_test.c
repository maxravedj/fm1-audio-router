/* SPDX-License-Identifier: MPL-2.0
 * Visible geometry and source isolation, using the actual app and PCM taps. */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static uint16_t saved[240*240];
static void turn(int n,int delta){enc_acc[n]=delta;app_input_only();}
static void start(unsigned mode,unsigned source)
{
    gateway_screen=GATEWAY_MAIN;gateway_mode=mode;gateway_monitor=source;
    gateway_aac_active=0;gateway_muted=0;gateway_view_changed();app_loop();
}
static void selected_meter(void)
{
    for(unsigned mode=GATEWAY_MODE_FULL;mode<=GATEWAY_MODE_SPECTRUM;mode++)for(unsigned source=0;source<4;source++){
        start(mode,source);memcpy(saved,fb,sizeof fb);
        gateway_meter[0]=(gateway_meter_t){.level=600};
        gateway_meter[1]=(gateway_meter_t){.level=300};gateway_draw_meter(1);
        unsigned footer_y=mode==GATEWAY_MODE_SPECTRUM?182:196;
        assert(!memcmp(saved,fb,240*footer_y*sizeof(uint16_t))); /* chart/header untouched */
        for(unsigned ch=0;ch<2;ch++)for(unsigned s=0;s<30;s++)for(unsigned y=222+ch*10;y<229+ch*10;y++){
            uint16_t expected=swap16((s+1)*20<=gateway_meter[ch].level?
                gateway_meter_color((s+1)*20):RGB(21,26,26));
            for(unsigned x=15+s*7;x<21+s*7;x++)assert(fb[y*240+x]==expected);
            assert(!fb[y*240+21+s*7]); /* one-pixel segment gaps */
        }
        for(unsigned x=0;x<240;x++)assert(!fb[229*240+x] && !fb[230*240+x] && !fb[231*240+x]);
        uint32_t before=blits;gateway_draw_meter(0);assert(blits==before);
        gateway_meter[0].held=540;gateway_meter[0].peak=1440;gateway_draw_meter(0);
        assert(memcmp(saved+196*240,fb+196*240,26*240*sizeof(uint16_t))); /* values above bars */
        if(source==PLAT_MONITOR_MASTER){
            gateway_muted=1;gateway_draw_meter(0);
            assert(fb[225*240+30]==swap16(gateway_meter_color(60))); /* Only Mixer grays muted meters. */
        }
    }
    puts("PASS: waveform/spectrum selected-source horizontal stereo meter, seven-pixel thickness, exact segments/gaps, values above, mute color preserved outside Mixer, chart/header preservation and idle skip");
}
static void fullpage_grid(void)
{
    start(GATEWAY_MODE_METERS,PLAT_MONITOR_MASTER);
    for(unsigned i=0;i<8;i++)gateway_meter[i]=(gateway_meter_t){.level=600};
    gateway_draw_meter(1);
    for(unsigned g=0;g<4;g++)for(unsigned ch=0;ch<2;ch++)for(unsigned s=0;s<32;s++){
        unsigned x=26+g*54+ch*18,y=204-s*5;
        uint16_t color=swap16(gateway_meter_color(((s+1)*600u+31u)/32u));
        for(unsigned j=0;j<4;j++)for(unsigned i=0;i<16;i++)assert(fb[(y+j)*240+x+i]==color);
        for(unsigned i=0;i<16;i++)assert(!fb[(y+4)*240+x+i]);
    }
    memcpy(saved,fb,sizeof fb);unsigned writes=store_writes;
    turn(EN_SELECT,-1);turn(EN_SELECT,1);app_loop();
    assert(gateway_monitor==PLAT_MONITOR_MASTER && writes==store_writes);
    for(unsigned g=0;g<4;g++)for(unsigned y=44;y<209;y++)
        assert(!memcmp(saved+y*240+26+g*54,fb+y*240+26+g*54,34*sizeof(uint16_t)));
    puts("PASS: all eight 16-pixel-wide Mode 1 bars have exactly 32 equal four-pixel segments and one-pixel gaps; SELECT is inactive and preserves bars/source");
}
static void spectrum_sources(void)
{
    start(GATEWAY_MODE_SPECTRUM,PLAT_MONITOR_USB);
    const int32_t pcm[][2]={{256000,-512000},{1280000,-1536000},{768000,-1024000}};
    for(unsigned source=0;source<3;source++){
        assert(gateway_monitor==source && !usb_spectrum_filled);
        uint32_t at=usb_spectrum_count;
        usb_input_meter_samples(pcm[0],1);plat_audio_monitor_bt_begin();
        plat_audio_monitor_bt_sample(pcm[1][0],pcm[1][1]);master_meter_samples(pcm[2],1);
        assert(usb_spectrum_count==at+1 && usb_spectrum_filled==1);
        unsigned i=at&(USB_SPECTRUM_RING-1);
        assert(usb_spectrum_ring[i][0]==pcm[source][0]/256 && usb_spectrum_ring[i][1]==pcm[source][1]/256);
        usb_input_stream_start();assert(usb_spectrum_filled==(source==PLAT_MONITOR_USB?0:1));
        for(i=0;i<PLAT_SPECTRUM_FRAMES;i++){
            int32_t tone=(int32_t)(4194304.0*sin(6.283185307179586*1000*i/44100.0));
            if(source==PLAT_MONITOR_USB){int32_t lr[]={tone,-tone};usb_input_meter_samples(lr,1);}
            else if(source==PLAT_MONITOR_BT){plat_audio_monitor_bt_begin();plat_audio_monitor_bt_sample(tone,-tone);}
            else {int32_t lr[]={tone,-tone};master_meter_samples(lr,1);}
        }
        now_ms+=33;app_loop();assert(gateway_spectrum_peak[22]>-10000);
        if(source<2){
            turn(EN_SELECT,source==1?2:1);assert(!usb_spectrum_filled);
            for(i=0;i<40;i++)assert(gateway_spectrum_peak[i]==GATEWAY_SPECTRUM_FLOOR);
        }
    }
    turn(EN_SELECT,1);assert(gateway_monitor==PLAT_MONITOR_USB); /* MASTER wraps */
    turn(EN_SELECT,-1);assert(gateway_monitor==PLAT_MONITOR_MASTER);
    turn(EN_SELECT,-1);assert(gateway_monitor==PLAT_MONITOR_OUT);
    turn(EN_SELECT,-1);assert(gateway_monitor==PLAT_MONITOR_BT);
    turn(EN_SELECT,1);assert(gateway_monitor==PLAT_MONITOR_OUT);
    turn(EN_SELECT,1);assert(gateway_monitor==PLAT_MONITOR_MASTER);
    turn(EN_SELECT,INT_MIN);assert(gateway_monitor==PLAT_MONITOR_MASTER);
    puts("PASS: SELECT cycles USB/BT/MASTER spectrum; isolated stereo capture, real FFT tone for every source, USB restart isolation and fresh source history/peaks");
}
static void master_wave(void)
{
    static int32_t lr[512];
    gateway_master_wave_auto_scale=1;gateway_wave_color=GATEWAY_WAVE_BLUE;
    start(GATEWAY_MODE_FULL,PLAT_MONITOR_MASTER);gateway_zoom=11;
    for(unsigned i=0;i<256;i++){lr[2*i]=768000;lr[2*i+1]=-1024000;}
    master_meter_samples(lr,256);gateway_capture_wave(now_ms);
    assert(gateway_wave_scale==4000);
    assert(gateway_wave_y(3000)<15 && gateway_wave_y(-4000)>=69);
    gateway_draw_wave(1);
    unsigned blue=0;
    for(unsigned y=26;y<184;y++)for(unsigned x=6;x<234;x++)if(fb[y*240+x]==swap16(RGB(45,130,255)))blue++;
    assert(blue>=228*2);
    memset(lr,0,sizeof lr);master_meter_samples(lr,256);for(unsigned i=0;i<600;i++)gateway_capture_wave(now_ms);
    assert(gateway_wave_scale>=32 && gateway_wave_scale<=38 && gateway_wave_y(0)==35);
    puts("PASS: selected MASTER waveform receives post-gain stereo PCM, remains visible at low levels through bounded display scaling, and silence stays centered");
}
int main(void)
{
    palette_set(APP_PALETTE);now_ms=100;app_init();app_ui_init();
    selected_meter();fullpage_grid();spectrum_sources();master_wave();
    assert(!cv_cuts && !cpu_work_depth);
}
