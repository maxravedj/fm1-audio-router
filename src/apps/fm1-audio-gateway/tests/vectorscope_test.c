/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static int32_t pcm[2u*GATEWAY_VECTOR_FRAMES];
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();app_loop();}
static void turn(unsigned e,int n){enc_acc[e]=n;app_input_only();app_loop();}
static void view(void){gateway_screen=GATEWAY_MAIN;gateway_mode=GATEWAY_MODE_VECTORSCOPE;gateway_view_changed();app_loop();app_frame();}
static unsigned points(void)
{
    unsigned n=0;for(unsigned y=0;y<GATEWAY_VECTOR_SIZE;y++)for(unsigned x=0;x<GATEWAY_VECTOR_SIZE;x++)
        n+=!!(gateway_vectorscope_buffers->plot[y][x>>3]&(1u<<(x&7u)));return n;
}
static void feed(unsigned source)
{
    if(source==PLAT_MONITOR_USB)usb_input_meter_samples(pcm,GATEWAY_VECTOR_FRAMES);
    else if(source==PLAT_MONITOR_BT){plat_audio_monitor_bt_begin();for(unsigned i=0;i<GATEWAY_VECTOR_FRAMES;i++)plat_audio_monitor_bt_sample(pcm[2*i],pcm[2*i+1]);}
    else if(source==PLAT_MONITOR_OUT)plat_audio_monitor_out_samples(pcm,GATEWAY_VECTOR_FRAMES);
    else master_meter_samples(pcm,GATEWAY_VECTOR_FRAMES);
}
static void capture_window(void)
{
    gateway_monitor=PLAT_MONITOR_USB;gateway_vectorscope_auto_gain=1;view();
    /* A short view warms up after 2048 pairs and never waits for an FFT window. */
    for(unsigned i=0;i<GATEWAY_VECTOR_FRAMES;i++){
        pcm[2*i]=(int32_t)i*256;pcm[2*i+1]=-(int32_t)i*256;
    }
    usb_input_meter_samples(pcm,GATEWAY_VECTOR_FRAMES-1u);
    gateway_vectorscope_capture(now_ms);assert(!gateway_vectorscope_buffers->have_position);
    usb_input_meter_samples(pcm+2*(GATEWAY_VECTOR_FRAMES-1u),1);
    gateway_vectorscope_capture(now_ms);assert(gateway_vectorscope_buffers->have_position);
    for(unsigned i=0;i<GATEWAY_VECTOR_FRAMES;i++){
        assert(gateway_vectorscope_buffers->pcm[i][0]==(int16_t)i);
        assert(gateway_vectorscope_buffers->pcm[i][1]==-(int16_t)i);
    }
    /* Old loud material must affect neither the new trace nor its linked gain. */
    for(unsigned i=0;i<GATEWAY_VECTOR_FRAMES;i++)pcm[2*i]=pcm[2*i+1]=8388352;
    for(unsigned block=0;block<3;block++)feed(PLAT_MONITOR_USB);
    for(unsigned i=0;i<GATEWAY_VECTOR_FRAMES;i++){pcm[2*i]=25600;pcm[2*i+1]=-51200;}
    feed(PLAT_MONITOR_USB);gateway_vectorscope_buffers->scale=0;
    gateway_vectorscope_capture(now_ms);assert(gateway_vectorscope_buffers->scale==200 && points()==1);
    /* Short/non-aligned reads wrap correctly and cannot overwrite the caller's bounds. */
    struct {int16_t before[2],data[33][2],after[2];} guarded;
    memset(&guarded,0x5a,sizeof guarded);uint32_t position=0;
    for(unsigned i=0;i<GATEWAY_VECTOR_FRAMES;i++){pcm[2*i]=(int32_t)i*256;pcm[2*i+1]=-(int32_t)i*256;}
    usb_spectrum_count=UINT32_MAX-511u;usb_spectrum_filled=0;feed(PLAT_MONITOR_USB);
    assert(plat_usb_input_spectrum_window(guarded.data,33,&position) && position==GATEWAY_VECTOR_FRAMES-512u);
    for(unsigned i=0;i<33;i++){
        int expected=(int)GATEWAY_VECTOR_FRAMES-33+(int)i;
        assert(guarded.data[i][0]==expected && guarded.data[i][1]==-expected);
    }
    assert(guarded.before[0]==0x5a5a && guarded.before[1]==0x5a5a && guarded.after[0]==0x5a5a && guarded.after[1]==0x5a5a);
    assert(!plat_usb_input_spectrum_window(guarded.data,0,&position));
    assert(!plat_usb_input_spectrum_window(guarded.data,PLAT_SPECTRUM_FRAMES+1,&position));
    now_ms+=100;assert(!plat_usb_input_spectrum_window(guarded.data,33,&position));
    puts("PASS: latest 2048 pairs, short-window warmup, old peaks excluded, bounded partial-chunk copies, ring/counter wrap and stale capture");
}
static void geometry(void)
{
    /* Independent geometric expectations, including both signed rails. */
    gateway_vectorscope_auto_gain=0;unsigned x,y;
    gateway_vectorscope_point(32767,32767,GATEWAY_VECTOR_UNITY,&x,&y);assert(x==88 && y==3);
    gateway_vectorscope_point(-32768,-32768,GATEWAY_VECTOR_UNITY,&x,&y);assert(x==88 && y==174);
    gateway_vectorscope_point(-32768,32767,GATEWAY_VECTOR_UNITY,&x,&y);assert(x==173 && y==88);
    gateway_vectorscope_point(32767,-32768,GATEWAY_VECTOR_UNITY,&x,&y);assert(x==3 && y==88);
    gateway_vectorscope_point(32767,0,GATEWAY_VECTOR_UNITY,&x,&y);assert(x==46 && y==46);
    gateway_vectorscope_point(0,32767,GATEWAY_VECTOR_UNITY,&x,&y);assert(x==130 && y==46);
    gateway_vectorscope_point(0,0,GATEWAY_VECTOR_UNITY,&x,&y);assert(x==88 && y==88);
    for(unsigned shape=0;shape<5;shape++){
        view();
        for(unsigned i=0;i<GATEWAY_VECTOR_FRAMES;i++){
            int l=((int)(i%1024)-512)*63,r=shape==1?-l:shape==2?0:l;
            if(shape==3){r=l;l=0;}if(shape==4)l=r=0;
            pcm[2*i]=l*256;pcm[2*i+1]=r*256;
        }
        feed(PLAT_MONITOR_USB);gateway_vectorscope_capture(now_ms);
        assert(points()>=(shape==4?1:60));
        for(unsigned yy=0;yy<176;yy++)for(unsigned xx=0;xx<176;xx++)if(gateway_vectorscope_buffers->plot[yy][xx>>3]&(1u<<(xx&7u))){
            if(shape==0)assert(xx==88);if(shape==1)assert(yy==88);
            if(shape==2)assert(xx==yy);if(shape==3)assert(xx+yy==176);
            if(shape==4)assert(xx==88 && yy==88);
        }
    }
    puts("PASS: mono/anti-phase/L-only/R-only/silence geometry and full-scale bounds");
}
static void auto_gain(void)
{
    gateway_monitor=PLAT_MONITOR_USB;gateway_vectorscope_auto_gain=1;view();
    for(unsigned level=32;level<=32768;level*=2){
        /* Constant ratio across channels: auto gain must not alter the image. */
        gateway_vectorscope_buffers->scale=0;
        for(unsigned i=0;i<GATEWAY_VECTOR_FRAMES;i++){
            pcm[2*i]=-(int32_t)level*256;pcm[2*i+1]=(int32_t)(level/2)*256;
        }
        now_ms+=33;feed(PLAT_MONITOR_USB);gateway_vectorscope_capture(now_ms);
        assert(gateway_vectorscope_buffers->scale==level && points()==1);
        assert(gateway_vectorscope_buffers->plot[109][152>>3]&(1u<<(152&7u)));
        assert(gateway_vectorscope_buffers->pcm[0][0]==-(int32_t)level);
    }
    /* Quiet input zooms in gradually; a louder transient expands immediately. */
    for(unsigned i=0;i<GATEWAY_VECTOR_FRAMES;i++)pcm[2*i]=pcm[2*i+1]=25600;
    uint32_t previous=gateway_vectorscope_buffers->scale;
    now_ms+=33;feed(PLAT_MONITOR_USB);gateway_vectorscope_capture(now_ms);
    assert(gateway_vectorscope_buffers->scale<previous && gateway_vectorscope_buffers->scale>100);
    now_ms=UINT32_MAX-10;gateway_vectorscope_buffers->gain_ms=now_ms;
    now_ms+=33;feed(PLAT_MONITOR_USB);gateway_vectorscope_capture(now_ms);
    assert(gateway_vectorscope_buffers->scale>=100);
    for(unsigned i=0;i<GATEWAY_VECTOR_FRAMES;i++){pcm[2*i]=-8388608;pcm[2*i+1]=8388352;}
    feed(PLAT_MONITOR_USB);gateway_vectorscope_capture(now_ms);assert(gateway_vectorscope_buffers->scale==32768);
    memset(pcm,0,sizeof pcm);gateway_vectorscope_buffers->scale=0;
    feed(PLAT_MONITOR_USB);gateway_vectorscope_capture(now_ms);assert(gateway_vectorscope_buffers->scale==32 && points()==1);
    now_ms+=100;gateway_vectorscope_capture(now_ms);assert(!gateway_vectorscope_buffers->scale && !points());
    turn(EN_SELECT,1);assert(!gateway_vectorscope_buffers->scale);
    gateway_vectorscope_auto_gain=0;
    puts("PASS: linked auto gain preserves channel ratio, floor/rails/silence, gentle recovery, immediate transient protection, clock wrap and stale/source reset");
}
static void sources(void)
{
    gateway_monitor=PLAT_MONITOR_USB;
    for(unsigned s=0;s<4;s++){
        view();unsigned source=gateway_source_order[s];assert(gateway_monitor==source);
        uint32_t count=usb_spectrum_count;
        for(unsigned other=0;other<4;other++)if(other!=source)feed(other);
        assert(usb_spectrum_count==count && !usb_spectrum_filled);
        for(unsigned i=0;i<GATEWAY_VECTOR_FRAMES;i++){pcm[2*i]=256000;pcm[2*i+1]=-512000;}
        feed(source);assert(usb_spectrum_count==count+GATEWAY_VECTOR_FRAMES);
        gateway_vectorscope_capture(now_ms);assert(points()==1 && gateway_vectorscope_buffers->pcm[0][0]==1000 && gateway_vectorscope_buffers->pcm[0][1]==-2000);
        now_ms+=100;gateway_vectorscope_capture(now_ms);assert(!points());
        feed(source);gateway_vectorscope_capture(now_ms);assert(points()==1);
        plat_audio_monitor_reset(source);gateway_vectorscope_capture(now_ms);assert(!points());
        turn(EN_SELECT,1);assert(!usb_spectrum_filled);
    }
    turn(EN_SELECT,-1);assert(gateway_monitor==PLAT_MONITOR_MASTER);
    turn(EN_SELECT,INT_MAX);assert(gateway_monitor==PLAT_MONITOR_OUT);
    turn(EN_SELECT,INT_MIN);assert(gateway_monitor==PLAT_MONITOR_OUT);
    puts("PASS: USB IN/BT IN/USB OUT/MASTER isolated capture, source cycle, stale/reset clearing and extreme turns");
}
static void rendering(void)
{
    gateway_monitor=PLAT_MONITOR_USB;view();
    for(unsigned shape=0;shape<4;shape++){
        for(unsigned i=0;i<GATEWAY_VECTOR_FRAMES;i++){
            double t=6.283185307179586*(double)i/GATEWAY_VECTOR_FRAMES;
            pcm[2*i]=(int32_t)(6000000*sin(19*t));pcm[2*i+1]=(int32_t)(6000000*sin((shape==1?19:23)*t+(shape==2?1.5:0)));
            if(shape==3)pcm[2*i]=pcm[2*i+1]=0;
        }
        now_ms+=33;feed(PLAT_MONITOR_USB);gateway_vectorscope_capture(now_ms);gateway_vectorscope_paint(0);
        static uint16_t expected[240*240];memcpy(expected,fb,sizeof fb);
        gateway_vectorscope_paint(1);assert(!memcmp(expected,fb,sizeof fb));
        unsigned bl=blits;gateway_vectorscope_capture(now_ms);gateway_vectorscope_paint(0);assert(blits==bl);
        uint32_t position=gateway_vectorscope_buffers->position;
        for(unsigned y=20;y<240;y+=11){
            plat_screenshot_begin(y);gateway_shot_paint();plat_screenshot_end();
            for(unsigned x=0;x<240;x++)assert(plat_screenshot_pixel(x)==swap16(expected[y*240+x]));
        }
        assert(blits==bl && gateway_vectorscope_buffers->position==position);
        const char *preview=getenv("FM1_VECTORSCOPE_PREVIEW");if(!shape && preview)png_write(preview);
    }
    puts("PASS: changing stereo clouds erase cleanly, dirty-strip/full-render agreement, zero unchanged LCD transfers, frozen screenshot rows");
}
static void settings(void)
{
    tap(B_EDIT);assert(GATEWAY_SET_VECTORSCOPE_COLOR==GATEWAY_SET_SPECTRUM_COLOR+1);
    assert(GATEWAY_SET_VECTOR_GAIN==GATEWAY_SET_AUTO_SCALE+1);
    gateway_settings_row=GATEWAY_SET_VECTORSCOPE_COLOR;turn(EN_ALGO,6);
    assert(gateway_vectorscope_color_draft==GATEWAY_WAVE_ORANGE && gateway_vectorscope_color==GATEWAY_WAVE_GREEN && gateway_settings_unsaved());
    store_fail=1;tap(B_SAVE);assert(gateway_vectorscope_color==GATEWAY_WAVE_GREEN && gateway_save_notice==2);
    store_fail=0;tap(B_SAVE);assert(gateway_vectorscope_color==GATEWAY_WAVE_ORANGE);
    tap(B_HOME);gateway_settings_t s=gateway_settings();assert(sizeof s==32 && (s.vectorscope_color&15u)==GATEWAY_WAVE_ORANGE);
    plat_store_save(OBJ_SETTINGS,&s,sizeof s);app_init();app_ui_init();app_loop();assert(gateway_has_vectorscope() && gateway_vectorscope_color==GATEWAY_WAVE_ORANGE);
    tap(B_EDIT);turn(EN_ALGO,-6);tap(B_HOME);assert(gateway_vectorscope_color==GATEWAY_WAVE_ORANGE);tap(B_EDIT);assert(gateway_vectorscope_color_draft==GATEWAY_WAVE_ORANGE);
    gateway_settings_row=GATEWAY_SET_VECTOR_GAIN;turn(EN_ALGO,1);
    assert(gateway_vector_gain_draft && !gateway_vectorscope_auto_gain && gateway_settings_unsaved());
    store_fail=1;tap(B_SAVE);assert(!gateway_vectorscope_auto_gain);
    store_fail=0;tap(B_SAVE);assert(gateway_vectorscope_auto_gain);
    app_init();assert(gateway_vectorscope_auto_gain && gateway_vectorscope_color==GATEWAY_WAVE_ORANGE);
    s=gateway_settings();s.version=20;s.vectorscope_color=GATEWAY_WAVE_RED;
    plat_store_save(OBJ_SETTINGS,&s,32);app_init();assert(gateway_vectorscope_color==GATEWAY_WAVE_RED && gateway_vectorscope_auto_gain);
    s=gateway_settings();s.vectorscope_color|=128;s.zoom=9;
    plat_store_save(OBJ_SETTINGS,&s,32);app_init();assert(gateway_zoom==6);
    s.version=19;s.mode=GATEWAY_MODE_SPECTRUM;s.vectorscope_color=0;s.midi_position[EN_K1]=25;
    plat_store_save(OBJ_SETTINGS,&s,32);app_init();assert(gateway_mode==GATEWAY_MODE_SPECTRUM && gateway_vectorscope_color==GATEWAY_WAVE_GREEN && gateway_midi_resume[EN_K1]==25);
    s.vectorscope_color=1;plat_store_save(OBJ_SETTINGS,&s,32);app_init();assert(gateway_mode==GATEWAY_MODE_METERS); /* v19 reserved byte remains validated */
    s=gateway_settings();s.zoom=9;s.vectorscope_color=GATEWAY_WAVE_COLORS;
    plat_store_save(OBJ_SETTINGS,&s,32);app_init();assert(gateway_zoom==6);
    puts("PASS: Vectorscope color/auto gain draft/discard/failed-save/restore, unchanged 32-byte record, v19/v20 migration and invalid-color rejection");
}
static void memory(void)
{
    app_ui_init();view();uint32_t used=sizeof work_host_storage-plat_work_available();
    assert(used==((sizeof(gateway_vectorscope_buffers_t)+7u)&~7u)+plat_usb_spectrum_mem_bytes());
    assert(used<=((sizeof(gateway_wave_buffers_t)+7u)&~7u)+plat_usb_wave_mem_bytes());
    tap(B_EDIT);assert(!gateway_vectorscope_buffers && !usb_spectrum_ring && !usb_spectrum_enabled && plat_work_available()==sizeof work_host_storage);
    uint8_t *reuse=plat_work_alloc(used);assert(reuse);memset(reuse,0xa5,used);feed(PLAT_MONITOR_USB);
    for(unsigned i=0;i<used;i++)assert(reuse[i]==0xa5);plat_work_free(reuse);
    tap(B_HOME);assert(gateway_has_vectorscope());btr_host_on=btr_host_connected=1;btr_host_codec=PLAT_BT_CODEC_AAC;
    app_bt_aac_prepare();app_loop();assert(!gateway_vectorscope_buffers && !usb_spectrum_enabled && gateway_wide_meters());
    btr_host_connected=0;app_loop();assert(gateway_has_vectorscope() && !usb_spectrum_filled);
    /* Decoder + maximum recording retain their budget, even while paused. */
    tap(B_EDIT);void *pressure=plat_work_alloc_top(plat_work_available()-96000);assert(pressure);
    void *record=plat_work_alloc_top(gateway_usb_out_bytes(GATEWAY_USB_BT_FX));assert(record);
    void *decoder=plat_work_alloc_top(23840);assert(decoder);
    btr_host_connected=1;btr_host_codec=PLAT_BT_CODEC_LDAC;btr_host_ldac_ready=1;tap(B_HOME);app_loop();
    assert(gateway_has_vectorscope());
    tap(B_EDIT);void *extra=plat_work_alloc_top(20000);assert(extra);tap(B_HOME);assert(!gateway_vectorscope_buffers && gateway_wide_meters());
    plat_work_free(extra);now_ms+=500;app_loop();assert(gateway_has_vectorscope());
    tap(B_EDIT);plat_work_free(decoder);plat_work_free(record);plat_work_free(pressure);btr_host_connected=0;btr_host_ldac_ready=0;app_loop();
    assert(plat_work_available()==sizeof work_host_storage && !cv_cuts && !cpu_work_depth);
    printf("PASS: %u-byte view workspace, no hidden producer/writes, AAC fallback/recovery, LDAC+recording admission and allocation retry\n",used);
}
int main(void){palette_set(APP_PALETTE);app_init();app_ui_init();capture_window();geometry();auto_gain();sources();rendering();settings();memory();}
