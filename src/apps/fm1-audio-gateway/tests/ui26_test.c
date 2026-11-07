/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void turn(unsigned e,int d){enc_acc[e]=d;app_input_only();}
int main(void){
 palette_set(APP_PALETTE);now_ms=1000;app_init();app_ui_init();app_loop();
 /* Release dispatch distinguishes taps from holds without accidentally navigating. */
 unsigned screen=gateway_screen;held_btn=1u<<B_FX;app_input_only();assert(gateway_screen==screen);
 now_ms+=999;app_input_only();assert(!gateway_fx_params.chain);
 now_ms++;app_input_only();assert(gateway_fx_params.chain && gateway_screen==screen);
 now_ms+=1000;app_input_only();assert(gateway_fx_params.chain);held_btn=0;app_input_only();assert(gateway_screen==screen);
 tap(B_FX);assert(gateway_screen==GATEWAY_FX && !gateway_fx_limiter);app_loop();
 assert(!gateway_page_clear);tap(B_SEL);assert(!gateway_fx_params.eq && (dim_btn&(1u<<B_SEL)));app_loop();
 tap(B_SEL);assert(gateway_fx_params.eq && (lights_b&(1u<<B_SEL)));
 gateway_fx_row=2;turn(EN_ALGO,1);unsigned slope=gateway_fx_params.slope[0];turn(EN_K2,1);assert(gateway_fx_params.slope[0]==slope+1);
 turn(EN_ALGO,-1);now_ms+=200;int gain=gateway_fx_params.band[0].gain;turn(EN_K2,1);assert(gateway_fx_params.band[0].gain==gain+1);
 for(unsigned i=0;i<12;i++){now_ms+=15;turn(EN_K2,1);}gain=gateway_fx_params.band[0].gain;
 now_ms+=15;turn(EN_K2,1);assert(gateway_fx_params.band[0].gain==gain+30);
 now_ms+=200;gain=gateway_fx_params.band[0].gain;turn(EN_K2,-1);assert(gateway_fx_params.band[0].gain==gain-1);
 tap(B_FX);assert(gateway_fx_limiter);tap(B_SEL);assert(!gateway_fx_params.lim);tap(B_SEL);
 fx_params_t p;fx_defaults(&p);p.chain=1;p.eq=0;p.ceiling=-60;gateway_fx_publish(&p);
 int32_t lr[512];for(unsigned block=0;block<8;block++){
   for(unsigned i=0;i<512;i++)lr[i]=7000000;app_master_process(lr,256,0);
   for(unsigned i=0;i<512;i++)assert(!lr[i]);
 }
 now_ms+=48;app_loop();assert(gateway_fx_meter[0].level>530 && gateway_fx_meter[0].level<550);
 assert(!cv_cuts && !cpu_work_depth);
 /* Badges: outline off, red disconnected, yellow pairing and connected codec. */
 gateway_bt_mode=0;gateway_draw_cpu();assert(fb[2*240+38]==swap16(C_GRAY));
 gateway_bt_mode=1;gateway_draw_cpu();assert(fb[2*240+38]==swap16(RGB(190,25,25)));
 plat_bt_receiver_on(1);plat_bt_receiver_pair(1);gateway_draw_cpu();assert(fb[2*240+38]==swap16(RGB(190,150,15)));
 hsx_session=0;gateway_draw_cpu();assert(fb[2*240+3]==swap16(RGB(190,25,25)));
 puts("PASS: FX short/long press, SEL/lights, K2 slope, 0.1..3dB acceleration and idle reset, zero MASTER post-limiter telemetry, shared status colors");
}
