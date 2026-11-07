/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void turn(int d){enc_acc[EN_SELECT]=d;app_input_only();app_loop();}
int main(void){
 palette_set(APP_PALETTE);app_init();app_ui_init();tap(B_EDIT);app_loop();
 for(unsigned row=1;row<8;row++){turn(1);assert(gateway_settings_row==row && gateway_settings_first==(row<6?0:row-5));}
 for(int row=6;row>=2;row--){turn(-1);assert(gateway_settings_row==row && gateway_settings_first==2);}
 turn(-1);assert(gateway_settings_row==1 && gateway_settings_first==1);
 turn(-1);assert(!gateway_settings_row && !gateway_settings_first);
 turn(INT_MAX);assert(gateway_settings_row==7 && gateway_settings_first==2);
 turn(-1);assert(gateway_settings_row==6 && gateway_settings_first==2);
 tap(B_HOME);tap(B_EDIT);assert(gateway_settings_row==6 && gateway_settings_first==2);
 turn(INT_MIN);assert(!gateway_settings_row && !gateway_settings_first);
 assert(!cv_cuts && !cpu_work_depth);
 puts("PASS: stable six-row Settings viewport within both edges, multi-detent/extreme clamps, scroll/re-entry and right scrollbar bounds");
}
