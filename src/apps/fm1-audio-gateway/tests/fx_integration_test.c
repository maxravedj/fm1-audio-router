/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void turn(unsigned e,int d){enc_acc[e]=d;app_input_only();}
static void row(unsigned r){turn(EN_SELECT,(int)r-(int)gateway_fx_row);assert(gateway_fx_row==r);}
static void controls(void)
{
    now_ms=100;app_init();app_ui_init();assert(!gateway_fx_params.chain);
    uint32_t available=plat_work_available();tap(B_HOME);assert(gateway_has_wave());tap(B_FX);
    assert(gateway_screen==GATEWAY_FX && !gateway_display_block && plat_work_available()==available);
    assert((lights_b&(1u<<B_FX)) && (dim_btn&(1u<<B_HOME)) && !(dim_btn&(1u<<B_SAVE)));
    unsigned writes=store_writes;held_btn=1u<<B_FX;app_input_only();now_ms+=1000;app_input_only();held_btn=0;app_input_only();assert(gateway_fx_params.chain && gateway_fx_dirty);
    now_ms+=749;gateway_fx_save(now_ms,0);assert(store_writes==writes);
    store_fail=1;now_ms++;app_loop();assert(gateway_fx_dirty && gateway_fx_notice==2);
    unsigned tries=store_tries;now_ms+=4999;app_loop();assert(store_tries==tries);
    store_fail=0;now_ms++;app_loop();assert(!gateway_fx_dirty && gateway_fx_notice==1 && store_writes==writes+2);
    row(1);turn(EN_K1,INT_MAX);assert(gateway_fx_params.post==120);tap(B_SEL);assert(!gateway_fx_params.eq);
    turn(EN_K1,INT_MIN);assert(gateway_fx_params.post==-240);tap(B_SEL);assert(gateway_fx_params.eq);
    for(unsigned i=0;i<5;i++){
        row(i+2);turn(EN_K1,INT_MAX);assert(gateway_fx_params.band[i].hz==19850);
        turn(EN_K1,INT_MIN);assert(gateway_fx_params.band[i].hz==20);
        turn(EN_K3,INT_MAX);assert(gateway_fx_params.band[i].q==1800);
        turn(EN_K3,INT_MIN);assert(gateway_fx_params.band[i].q==10);
        for(unsigned type=0;type<FX_TYPES;type++){
            int old=gateway_fx_params.band[i].gain;now_ms+=200;turn(EN_K2,1);
            assert(gateway_fx_params.band[i].gain==old+((type==FX_LOW_PASS || type==FX_HIGH_PASS)?0:1));
            app_loop();turn(EN_ALGO,1);
        }
        turn(EN_K4,-1);assert(gateway_fx_params.band[i].on);turn(EN_K4,1);assert(gateway_fx_params.band[i].on);
    }
    tap(B_FX);assert(gateway_fx_limiter);
    /* A slow detent is exactly 1 ms everywhere, including high releases where
     * the old percentage mapping skipped multiple milliseconds. */
    for(unsigned ms=11;ms<1000;ms++){
        fx_params_t p=gateway_fx_params;p.release=ms;gateway_fx_publish(&p);
        now_ms+=200;turn(EN_K2,1);assert(gateway_fx_params.release==ms+1);
        now_ms+=200;turn(EN_K2,-1);assert(gateway_fx_params.release==ms);
    }
    fx_params_t fine=gateway_fx_params;fine.release=100;gateway_fx_publish(&fine);
    now_ms+=200;turn(EN_K2,1);assert(gateway_fx_params.release==101);
    for(unsigned i=0;i<12;i++){now_ms+=15;turn(EN_K2,1);}
    unsigned release=gateway_fx_params.release;now_ms+=15;turn(EN_K2,1);
    assert(gateway_fx_params.release==release+30);
    release=gateway_fx_params.release;now_ms+=200;turn(EN_K2,-1);
    assert(gateway_fx_params.release==release-1);
    puts("PASS: limiter release has exact 1-ms slow detents across its range, accelerated 30-ms detents and idle reset");
    turn(EN_K1,INT_MAX);turn(EN_K2,INT_MAX);turn(EN_K3,INT_MIN);
    assert(gateway_fx_params.input==240 && gateway_fx_params.release==1000 && gateway_fx_params.ceiling==-240);
    turn(EN_K1,INT_MIN);turn(EN_K2,INT_MIN);turn(EN_K3,INT_MAX);tap(B_SEL);
    assert(gateway_fx_params.input==-240 && gateway_fx_params.release==10 && !gateway_fx_params.ceiling && !gateway_fx_params.lim);
    tap(B_SAVE);assert(gateway_fx_dialog==1);tap(B_OCTUP);assert(!gateway_fx_dirty && gateway_fx_notice==1);
    tap(B_FX);tap(B_SAVE);tap(B_OCTUP); /* Save EQ separately from limiter. */
    fx_params_t expected=gateway_fx_params;tap(B_HOME);app_init();app_ui_init();
    assert(!memcmp(&expected,&gateway_fx_params,sizeof expected));
    gateway_fx_presets_t saved;assert(plat_store_load(OBJ_FX,&saved,sizeof saved)==sizeof saved && saved.version==4);
    gateway_fx_record_t r;
    /* CRC-valid but semantically invalid/mis-sized records must leave neutral defaults. */
    for(unsigned bad=0;bad<7;bad++){
        r=(gateway_fx_record_t){{'A','F','X'},2,expected};unsigned size=sizeof r;
        if(bad==0)r.magic[0]='?';if(bad==1)r.version=3;if(bad==2)size--;
        if(bad==3)r.params.band[0].type=5;if(bad==4)r.params.release=0;
        if(bad==5)r.params.chain=2;if(bad==6)r.params.reserved=1;
        plat_store_save(OBJ_FX,&r,size);app_init();assert(!gateway_fx_params.chain && gateway_fx_params.release==100);
    }
    assert(!cv_cuts && !cpu_work_depth);
    puts("PASS: FX navigation/lights, all 5 bands/types and bounded knobs including INT_MIN/MAX, live publication, A/B object persistence, debounce/failure/retry, malformed defaults, no display scratch");
}
static void mixer(void)
{
    static int32_t usb[2*APP_BLOCK],out[2*APP_BLOCK];static int16_t bt[2*APP_BLOCK];
    uint32_t peak[2];plat_bt_receiver_on(1);plat_usb_play_gain(4096);plat_master_mute_init(0);
    plat_usb_input_observe(1,0);plat_master_meter_enable(1);
    for(unsigned i=0;i<APP_BLOCK;i++){usb[2*i]=8000000;usb[2*i+1]=-4000000;bt[2*i]=16000;bt[2*i+1]=-8000;}
    usb_play_buf=usb;usb_play_n=APP_BLOCK;
    /* Sum exceeds 0 dBFS, but half MASTER attenuates it WITHOUT prior clipping. */
    for(unsigned full=0;full<2;full++){
        uac_full=full;master=2048;gateway_fx_init();btr_audio_reset(44100);
        for(unsigned i=0;i<BTR_TARGET/APP_BLOCK;i++)btr_pcm_put(bt,APP_BLOCK);
        uint32_t read=btr_r;usb_play_at=0;sim_render(out);
        int expected=(int)((8000000+4096000)*(APP_MASTER_FX_GAIN(master)/4096.0f));
        assert(abs(out[2*FX_DELAY]-expected)<100 && abs(out[2*FX_DELAY+1]+expected/2)<100);
        assert(usb_play_at==APP_BLOCK && btr_r-read>=255 && btr_r-read<=257);
        plat_usb_input_peaks(peak);assert(peak[0]==8000000 && peak[1]==4000000);
        plat_bt_input_peaks(peak);assert(abs((int)peak[0]-4096000)<512);
    }
    /* The actual shared mixer must put the limiter on both sources BEFORE MASTER. */
    fx_params_t p;fx_defaults(&p);p.chain=1;p.eq=0;p.ceiling=-60;
    gateway_fx_publish(&p);master=4096;
    for(unsigned k=0;k<8;k++){
        usb_play_at=0;sim_render(out);int cap=(int)(fm_db2lin(-6)*8388608.0f)+2;
        for(unsigned i=0;i<APP_BLOCK;i++){
            if(k || i>=FX_BYPASS_FADE-1)assert(abs(out[2*i])<=cap && abs(out[2*i+1])<=cap);
            if(!k && i==FX_BYPASS_FADE-1)assert(!out[2*i] && !out[2*i+1]);
        }
    }
    assert(gateway_fx.reduction_q16<40000 && gateway_fx.seen==gateway_fx.generation);
    plat_master_mute_init(1);usb_play_at=0;sim_render(out);plat_master_peaks(peak);
    for(unsigned i=0;i<512;i++)assert(!out[i]);assert(peak[0]>1000000); /* meter precedes mute */
    usb_rec_f=tmpfile();assert(usb_rec_f);usb_rec_frames=0;usb_play_at=0;sim_render(out);
    assert(usb_rec_frames==APP_BLOCK);rewind(usb_rec_f);for(unsigned i=0;i<APP_BLOCK*4;i++)assert(fgetc(usb_rec_f)==0);
    fclose(usb_rec_f);usb_rec_f=0;
    usb_play_buf=0;plat_bt_receiver_on(0);
    puts("PASS: shared device mixer, USB/BT consumed once, headroom before MASTER and limiter, full modes equivalent, isolated input taps, MASTER before mute, USB recording excludes playback");
}
int main(void){setbuf(stdout,NULL);palette_set(APP_PALETTE);controls();mixer();assert(!cv_cuts && !cpu_work_depth);}
