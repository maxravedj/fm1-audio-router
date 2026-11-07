/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void turn(unsigned e,int d){enc_acc[e]=d;app_input_only();}
static void select_row(unsigned row){turn(EN_SELECT,(int)row-gateway_fx_row);assert(gateway_fx_row==row);}
static void migration(void)
{
    fx_params_t p;fx_defaults(&p);p.chain=1;p.post=-25;p.band[2].gain=90;
    gateway_fx_record_t old={{'A','F','X'},1,p};
    assert(sizeof(fx_params_t)==58 && sizeof(gateway_fx_record_t)==62);
    plat_store_save(OBJ_FX,&old,56);app_init();app_ui_init();
    assert(gateway_fx_params.chain && gateway_fx_params.post==-25 && gateway_fx_params.band[2].gain==90);
    for(unsigned i=0;i<5;i++)assert(!gateway_fx_params.slope[i]);
    tap(B_FX);assert(!gateway_fx_limiter && gateway_fx_row==2);
    turn(EN_ALGO,1);turn(EN_K2,2);assert(gateway_fx_params.slope[0]==2);
    tap(B_SAVE);assert(gateway_fx_dialog==1);tap(B_OCTUP);gateway_fx_presets_t saved;
    assert(plat_store_load(OBJ_FX,&saved,sizeof saved)==sizeof saved && saved.version==4 && saved.eq[0].slope[0]==2);
    app_init();app_ui_init();assert(gateway_fx_params.slope[0]==2 && gateway_fx_params.band[2].gain==90);
    saved.eq[0].slope[0]=4;plat_store_save(OBJ_FX,&saved,sizeof saved);app_init();app_ui_init();
    assert(!gateway_fx_params.chain && !gateway_fx_params.slope[0]);
    puts("PASS: AFX v1 56-byte migration preserves every old control and defaults to 12 dB/oct; v4 preset slopes save/reboot and malformed slopes default safely");
}
static void eq(void)
{
    tap(B_HOME);assert(gateway_has_wave());unsigned available=plat_work_available();tap(B_FX);app_loop();
    assert(!gateway_fx_limiter && gateway_fx_row==2 && !gateway_display_block && plat_work_available()>available && !gateway_fx.meter_enabled);
    assert(!usb_input_wave_enabled && !usb_spectrum_enabled && !master_meter_enabled);
    for(unsigned type=0;type<FX_TYPES;type++){
        gateway_fx_params.band[0].type=type;
        for(unsigned slope=0;slope<4;slope++){
            gateway_fx_params.slope[0]=slope;gateway_page_redraw=1;app_loop();assert(!cv_cuts);
            for(unsigned i=0;i<5;i++)assert(fb[170*240+i*48+24]);
        }
    }
    fx_params_t p;fx_defaults(&p);p.chain=1;p.band[2].gain=60;
    uint8_t curves[6][FX_GRAPH_W];gateway_fx_curves(&p,curves);
    unsigned at=gateway_fx_x(1000)-FX_GRAPH_X;
    assert(abs(curves[2][at]-gateway_fx_y(6))<=1 && curves[5][at]==curves[2][at]);
    p.band[2].on=0;gateway_fx_curves(&p,curves);assert(curves[5][at]==87);
    p.band[2].on=1;p.chain=0;gateway_fx_curves(&p,curves);assert(curves[5][at]==curves[2][at] && curves[2][at]<87);
    p.chain=1;p.band[0].type=FX_HIGH_PASS;p.band[0].hz=1000;p.slope[0]=3;
    gateway_fx_curves(&p,curves);assert(curves[0][0]==FX_GRAPH_BOTTOM && abs(curves[0][at]-gateway_fx_y(-3))<=1);
    gateway_fx_publish(&p);select_row(2);turn(EN_K2,INT_MIN);assert(!gateway_fx_params.slope[0]);
    turn(EN_K2,INT_MAX);assert(gateway_fx_params.slope[0]==3);
    turn(EN_ALGO,-2);assert(gateway_fx_params.band[0].type==FX_BELL);turn(EN_K2,-1);assert(gateway_fx_params.slope[0]==3);
    char text[40];gateway_fx_format(text,"",1985,2,"k");assert(!strcmp(text,"19.85k"));
    gateway_fx_format(text,"",-240,1,"dB");assert(text_w(&FONT_T,text)<47);
    gateway_fx_format(text,"R",1800,2,"");assert(text_w(&FONT_T,text)<47);
    puts("PASS: five visible equal-width band cards, all type pictograms/slopes, selected controls, compact values, log response/combined bypass curves and slope encoder gates/clamps; no FFT/wave/limiter capture on EQ");
}
static void limiter(void)
{
    unsigned selected=gateway_fx_row;tap(B_FX);assert(gateway_fx_limiter && gateway_fx.meter_enabled);
    held_btn=1u<<B_FX;app_input_only();assert(gateway_fx_limiter);
    for(unsigned i=0;i<30;i++)app_input_only();assert(gateway_fx_limiter);held_btn=0;app_input_only();
    tap(B_FX);assert(gateway_fx_limiter && gateway_fx_row==selected);
    turn(EN_SELECT,INT_MAX);assert(gateway_fx_row==selected);
    turn(EN_K1,24);turn(EN_K3,-10);
    fx_params_t p;fx_defaults(&p);p.chain=1;p.eq=0;p.input=120;p.ceiling=-60;
    gateway_fx_publish(&p);int32_t lr[512];
    for(unsigned block=0;block<8;block++){
        /* Measure settled limited output. During enable, premute telemetry
         * legitimately includes the old bypass path until the silent switch;
         * it must not be forced under the pending ceiling just for this test. */
        if(block==2){gateway_fx.output_peak[0]=gateway_fx.output_peak[1]=0;gateway_fx.meter_reduction=65536;}
        for(unsigned i=0;i<256;i++){lr[2*i]=7000000;lr[2*i+1]=-3500000;}
        app_master_process(lr,256,4096);
    }
    now_ms+=48;app_loop();
    assert(gateway_fx_meter[0].level>530 && gateway_fx_meter[0].level<550 && gateway_fx_reduction>150);
    unsigned red=0;for(unsigned y=50;y<230;y++)if(fb[y*240+226]==swap16(RGB(255,55,60)))red++;
    assert(red>60);
    gateway_muted=1;gateway_fx.meter_reduction=16384;gateway_fx.output_peak[0]=4194304;now_ms+=48;app_loop();
    assert(fb[131*240+166]==swap16(RGB(40,195,100)) && gateway_fx_reduction>0);gateway_muted=0;
    for(unsigned i=0;i<100;i++){now_ms+=48;app_loop();}
    assert(gateway_fx_reduction==0 && !gateway_fx_meter[0].level);
    tap(B_FX);assert(!gateway_fx_limiter && !gateway_fx.meter_enabled && gateway_fx_row==selected);
    app_loop();assert(!cv_cuts);
    tap(B_HOME);assert(!gateway_fx.meter_enabled && gateway_has_wave());
    puts("PASS: FX toggles EQ/limiter once per press, preserves band selection; actual pre-mute stereo level and red gain reduction, pre-mute level, decay/silence, hidden meter producer stops");
}
int main(void){setbuf(stdout,NULL);palette_set(APP_PALETTE);now_ms=100;migration();eq();limiter();assert(!cv_cuts && !cpu_work_depth);}
