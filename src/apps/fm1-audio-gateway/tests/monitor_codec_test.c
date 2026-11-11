/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static void tap(int b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void turn(int e,int d){enc_acc[e]=d;app_input_only();}
static uint32_t hash_area(unsigned x,unsigned y,unsigned w,unsigned h)
{
    uint32_t v=2166136261u;
    for(unsigned j=y;j<y+h;j++)for(unsigned i=x;i<x+w;i++)v=(v^fb[j*240+i])*16777619u;
    return v;
}
static void settings(void)
{
    gateway_settings_t s;now_ms=100;app_init();app_ui_init();app_loop();
    tap(B_EDIT);turn(EN_SELECT,1);tap(B_SEL);tap(B_SEL); /* SBC off, AAC only */
    assert(gateway_codec_draft==2 && gateway_codec_preference==3 && btr_codec_mask==3);
    store_fail=1;tap(B_SAVE);assert(gateway_save_notice==2 && btr_codec_mask==3 && gateway_codec_dialog);
    store_fail=0;tap(B_SAVE);assert(gateway_save_notice==1 && btr_codec_mask==2 && !gateway_codec_dialog);
    assert(plat_store_load(OBJ_SETTINGS,&s,sizeof s)==sizeof s && s.version==17 && s.bt_reserved[0]==2);
    app_init();app_ui_init();assert(gateway_codec_preference==2);
    s=gateway_settings();s.version=4;s.bt_reserved[0]=0;s.spectrum_range=3;s.reserved=s.spectrum_reserved=0;memset(s.bt_reserved,0,3);s.zoom=9;
    plat_store_save(OBJ_SETTINGS,&s,16);app_init();app_ui_init();
    assert(gateway_zoom==9 && !gateway_monitor && gateway_codec_preference==3);
    s=gateway_settings();s.bt_reserved[0]=0;plat_store_save(OBJ_SETTINGS,&s,sizeof s);app_init();app_ui_init();
    assert(gateway_zoom==6 && gateway_codec_preference==3);
    puts("PASS: codec mask draft/save/failure/restore, v4 migration and invalid-mask defaults");
}
static void capture_sources(void)
{
    int32_t usb[]={256000,-512000},master_pcm[]={768000,-1024000};
    uint32_t peak[2],at;int16_t v[4];
    gateway_mode=GATEWAY_MODE_FULL;gateway_monitor=PLAT_MONITOR_USB;gateway_view_changed();
    at=usb_wave_count;usb_input_meter_samples(usb,1);plat_audio_monitor_bt_begin();plat_audio_monitor_bt_sample(1280000,-1536000);master_meter_samples(master_pcm,1);
    assert(usb_wave_count==at+1 && usb_wave_read(at,0,v) && v[0]==1000 && v[2]==-2000);
    turn(EN_SELECT,1);assert(gateway_monitor==PLAT_MONITOR_BT && !usb_wave_filled);
    at=usb_wave_count;usb_input_meter_samples(usb,1);master_meter_samples(master_pcm,1);
    assert(usb_wave_count==at);plat_audio_monitor_bt_begin();plat_audio_monitor_bt_sample(1280000,-1536000);
    assert(usb_wave_count==at+1 && usb_wave_read(at,0,v) && v[0]==5000 && v[2]==-6000);
    plat_bt_input_peaks(peak);assert(peak[0]==1280000 && peak[1]==1536000);
    plat_bt_input_peaks(peak);assert(!peak[0] && !peak[1]);
    usb_input_stream_start();assert(usb_wave_filled==1); /* USB restart cannot erase BT trace */
    plat_audio_monitor_bt_sample(INT_MAX,INT_MIN);assert(usb_wave_read(at+1,0,v) && v[0]==32767 && v[2]==-32768);
    btr_audio_reset(44100);assert(!usb_wave_filled); /* reconnect never revives stale BT */
    turn(EN_SELECT,2);assert(gateway_monitor==PLAT_MONITOR_MASTER);
    at=usb_wave_count;usb_input_meter_samples(usb,1);plat_audio_monitor_bt_sample(1280000,-1536000);
    assert(usb_wave_count==at);master_meter_samples(master_pcm,1);
    assert(usb_wave_count==at+1 && usb_wave_read(at,0,v) && v[0]==3000 && v[2]==-4000);
    btr_audio_reset(44100);usb_input_stream_start();assert(usb_wave_filled==1);
    now_ms+=750;app_loop();gateway_settings_t s;
    assert(plat_store_load(OBJ_SETTINGS,&s,sizeof s)==sizeof s && s.bt_reserved[1]==PLAT_MONITOR_MASTER);
    app_init();app_ui_init();assert(gateway_monitor==PLAT_MONITOR_MASTER && !usb_wave_filled);
    gateway_mode=GATEWAY_MODE_METERS;gateway_view_changed();assert(gateway_mode==GATEWAY_MODE_METERS && !usb_wave_history);
    at=usb_wave_count;usb_input_meter_samples(usb,1);plat_audio_monitor_bt_sample(1280000,-1536000);master_meter_samples(master_pcm,1);
    assert(usb_wave_count==at); /* no hidden waveform work for any source */
    app_loop();now_ms+=48;app_frame();
    assert(gateway_meter[0].level && gateway_meter[2].level && gateway_meter[4].level);
    uint32_t usb_hash=hash_area(26,44,34,165),bt_hash=hash_area(80,44,34,165),master_hash=hash_area(188,44,34,165);
    assert(usb_hash && bt_hash && master_hash);
    uint32_t header=hash_area(0,0,240,22);gateway_draw_meter(1);assert(header==hash_area(0,0,240,22));
    tap(B_PLAY);app_loop();app_frame();
    assert(usb_hash==hash_area(26,44,34,165) && bt_hash==hash_area(80,44,34,165));
    assert(master_hash==hash_area(188,44,34,165)); /* muted meters gray only on Mixer */
    turn(EN_SELECT,-1);assert(gateway_monitor==PLAT_MONITOR_MASTER);
    app_loop();app_frame(); /* SELECT is inactive on meters. */
    tap(B_EDIT);at=usb_wave_count;
    usb_input_meter_samples(usb,1);plat_audio_monitor_bt_sample(1280000,-1536000);master_meter_samples(master_pcm,1);
    plat_bt_input_peaks(peak);assert(!peak[0] && !peak[1] && at==usb_wave_count);
    puts("PASS: isolated USB/BT/MASTER stereo capture, clipping, reconnect reset, monitor persistence, three visible meters, mute color, CPU/badge preservation and zero hidden capture");
}
static void mix_taps(void)
{
    static int32_t usb[2*APP_BLOCK],out[2*APP_BLOCK],reference[2*APP_BLOCK];static int16_t bt[2*APP_BLOCK];
    uint32_t peaks[2];plat_bt_receiver_on(1);master=2048;
    for(unsigned i=0;i<APP_BLOCK;i++){usb[2*i]=1000000;usb[2*i+1]=-500000;bt[2*i]=8000;bt[2*i+1]=-4000;}
    usb_play_buf=usb;usb_play_n=APP_BLOCK;
    gateway_screen=GATEWAY_MAIN;gateway_mode=GATEWAY_MODE_FULL;
    for(unsigned source=0;source<3;source++){
        gateway_monitor=source;gateway_view_changed();btr_audio_reset(44100);
        for(unsigned i=0;i<BTR_TARGET/APP_BLOCK;i++)btr_pcm_put(bt,APP_BLOCK);
        usb_play_at=0;plat_master_mute_init(0);gateway_fx_init();sim_render(out);
        if(!source)memcpy(reference,out,sizeof out);else assert(!memcmp(reference,out,sizeof out));
        int expected=(int)(3048000LL*APP_MASTER_FX_GAIN(master)/4096);
        assert(abs(out[2*FX_DELAY]-expected)<100 && abs(out[2*FX_DELAY+1]+expected/2)<100);
        plat_bt_input_peaks(peaks);assert(abs((int)peaks[0]-2048000)<100 && abs((int)peaks[1]-1024000)<100);
        assert(usb_wave_filled==APP_BLOCK); /* exactly one selected producer */
        int16_t last[4];assert(usb_wave_read(usb_wave_count-1,0,last));
        int sample=source==0?1000000:source==1?2048000:expected;
        assert(abs(last[0]-sample/256)<=1);
    }
    usb_play_buf=0;plat_bt_receiver_on(0);
    puts("PASS: actual FIR output feeds BT tap before MASTER; monitor selection leaves USB+BT mix bit-identical and MASTER applied once");
}
int main(void){palette_set(APP_PALETTE);settings();capture_sources();mix_taps();assert(!cv_cuts && !cpu_work_depth);}
