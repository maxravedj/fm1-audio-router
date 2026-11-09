/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void turn(unsigned e,int d){enc_acc[e]=d;app_input_only();}
static void save(void){unsigned writes=store_writes;tap(B_SAVE);assert(gateway_fx_dialog==1 && store_writes==writes);tap(B_OCTUP);assert(!gateway_fx_dialog && store_writes==writes+1);}
static void hold_save(void){held_btn=1u<<B_SAVE;app_input_only();now_ms+=999;app_input_only();assert(!gateway_fx_dialog);now_ms++;app_input_only();assert(gateway_fx_dialog==2);held_btn=0;app_input_only();}
static void reboot(void){held_btn=0;app_init();app_ui_init();tap(B_FX);assert(gateway_screen==GATEWAY_FX && !gateway_fx_limiter);}
static void title(void){
 assert(!!(lights_b&(1u<<B_SAVE))==gateway_fx_preset_dirty(gateway_fx_limiter));
 assert(!(dim_btn&(1u<<B_SAVE)));
 app_loop();const char *t=gateway_fx_limiter?"FX: PEAK LIMITER":"FX: EQUALIZER";char label[40];
 gateway_fx_format(label,"Preset ",gateway_fx_selected[gateway_fx_limiter]+1,0,"");
 static uint16_t actual[240*16];memcpy(actual,fb+18*240,sizeof actual);
 cv_begin(240,16,C_BLACK);cv_text(5,1,&FONT_T,t,(gateway_fx_limiter?gateway_fx_params.lim:gateway_fx_params.eq)?C_WHITE:C_GRAY);
 cv_text(235-text_w(&FONT_T,label),1,&FONT_T,label,gateway_fx_preset_dirty(gateway_fx_limiter)?RGB(255,212,40):C_WHITE);cv_blit(0,18);
 assert(!memcmp(actual,fb+18*240,sizeof actual));
}
static void dialog_text(void){
 app_loop();char expected[40];snprintf(expected,sizeof expected,"%s preset %u?",gateway_fx_dialog==2?"Initialize":"Save",gateway_fx_selected[gateway_fx_limiter]+1);
 static uint16_t actual[216*12];
 for(unsigned y=0;y<12;y++)memcpy(actual+y*216,fb+(100+y)*240+12,216*sizeof(uint16_t));
 cv_begin(216,12,RGB(18,24,31));cv_rect(0,0,1,12,C_GRAY);cv_rect(215,0,1,12,C_GRAY);
 cv_text((216-text_w(&FONT_T,expected))/2,0,&FONT_T,expected,C_WHITE);cv_blit(12,100);
 for(unsigned y=0;y<12;y++)assert(!memcmp(actual+y*216,fb+(100+y)*240+12,216*sizeof(uint16_t)));
 /* Independent pixel oracle: widely separated choices, panel labels on the
  * next row. Initialize must say YES rather than SAVE. */
 static uint16_t options[216*32];
 for(unsigned y=0;y<32;y++)memcpy(options+y*216,fb+(127+y)*240+12,216*sizeof(uint16_t));
 const char *right=gateway_fx_dialog==2?"YES":"SAVE";
 for(unsigned strip=0;strip<32;strip+=16){
  cv_begin(216,16,RGB(18,24,31));cv_oy=-(int)strip;
  cv_rect(0,0,1,32,C_GRAY);cv_rect(215,0,1,32,C_GRAY);
  cv_text((108-text_w(&FONT_T,"CANCEL"))/2,0,&FONT_T,"CANCEL",C_WHITE);
  cv_text(108+(108-text_w(&FONT_T,right))/2,0,&FONT_T,right,C_WHITE);
  cv_text((108-text_w(&FONT_T,"OCT-"))/2,16,&FONT_T,"OCT-",C_GRAY);
  cv_text(108+(108-text_w(&FONT_T,"OCT+"))/2,16,&FONT_T,"OCT+",C_GRAY);
  cv_oy=0;cv_blit(12,127+strip);
 }
 for(unsigned y=0;y<32;y++)assert(!memcmp(options+y*216,fb+(127+y)*240+12,216*sizeof(uint16_t)));
}
static void independent_bypass(void){
 store_len[OBJ_FX]=-1;reboot();assert(!(dim_btn&(1u<<B_SAVE)));
 tap(B_SEL);assert(!gateway_fx_params.eq && !gateway_fx_preset_dirty(0) && !(dim_btn&(1u<<B_SAVE)));
 now_ms+=750;app_loop();reboot();assert(!gateway_fx_params.eq && gateway_fx_params.lim);
 for(unsigned i=0;i<8;i++){turn(EN_PRESET,1);assert(!gateway_fx_params.eq && gateway_fx_params.lim);}
 now_ms+=200;turn(EN_K1,1);title();save();title();
 /* Same audio values, different dirty state: selection must refresh SAVE even
  * when parameter publication is a no-op. */
 turn(EN_PRESET,1);now_ms+=200;turn(EN_K1,1);title();
 assert(gateway_fx_preset_dirty(0));turn(EN_PRESET,-1);title();assert(!gateway_fx_preset_dirty(0));
 turn(EN_PRESET,1);title();assert(gateway_fx_preset_dirty(0));
 hold_save();dialog_text();tap(B_OCTUP);assert(!gateway_fx_params.eq && gateway_fx_params.lim);title();
 tap(B_SEL);assert(gateway_fx_params.eq && !gateway_fx_preset_dirty(0));
 turn(EN_PRESET,-1);assert(gateway_fx_params.eq); /* Saved while bypassed. */
 tap(B_FX);tap(B_SEL);assert(!gateway_fx_params.lim && !gateway_fx_preset_dirty(1));title();
 now_ms+=200;turn(EN_K1,1);title();save();title();
 for(unsigned i=0;i<8;i++){turn(EN_PRESET,1);assert(gateway_fx_params.eq && !gateway_fx_params.lim);}
 hold_save();dialog_text();tap(B_OCTUP);assert(!gateway_fx_params.lim && gateway_fx_params.eq);title();
 now_ms+=750;app_loop();reboot();assert(gateway_fx_params.eq && !gateway_fx_params.lim);
 gateway_fx_presets_t bank;assert(plat_store_load(OBJ_FX,&bank,sizeof bank)==456 && bank.enabled==1 && bank.version==4);
 for(unsigned i=0;i<8;i++)assert(!bank.eq[i].reserved && !bank.lim[i].reserved[0] && !bank.lim[i].reserved[1]);
 tap(B_HOME);assert(!(dim_btn&(1u<<B_SAVE)));tap(B_EDIT);assert(dim_btn&(1u<<B_SAVE));
 /* Migrate the actual v3 byte layout, including different flags per slot. */
 bank.version=3;bank.enabled=0;bank.selected[0]=2;bank.selected[1]=5;
 for(unsigned i=0;i<8;i++){bank.eq[i].reserved=i%2;bank.lim[i].reserved[0]=i%2;}
 bank.eq[2].post=24;bank.lim[5].ceiling=-70;
 assert(!plat_store_save(OBJ_FX,&bank,sizeof bank));reboot();
 assert(gateway_fx_selected[0]==2 && gateway_fx_selected[1]==5 && !gateway_fx_params.eq && gateway_fx_params.lim);
 assert(gateway_fx_params.post==24 && gateway_fx_params.ceiling==-70 && !gateway_fx_preset_dirty(0));
 for(unsigned i=0;i<8;i++)assert(!gateway_eq_work[i].reserved && !gateway_lim_work[i].reserved[0]);
 turn(EN_PRESET,1);assert(!gateway_fx_params.eq && gateway_fx_params.lim);now_ms+=750;app_loop();
 assert(plat_store_load(OBJ_FX,&bank,sizeof bank)==456 && bank.version==4 && bank.enabled==2 && bank.eq[2].post==24 && bank.lim[5].ceiling==-70);
 /* Legacy single-snapshot formats retain effect flags as metadata too. */
 for(unsigned version=1;version<=2;version++){
  gateway_fx_record_t old={{'A','F','X'},version,{0}};fx_defaults(&old.params);
  old.params.eq=old.params.lim=0;old.params.post=31;old.params.release=321;
  assert(!plat_store_save(OBJ_FX,&old,version==1?56:62));reboot();
  assert(!gateway_fx_params.eq && !gateway_fx_params.lim && gateway_fx_params.post==31 && gateway_fx_params.release==321);
  turn(EN_PRESET,1);assert(!gateway_fx_params.eq && !gateway_fx_params.lim && !gateway_fx_params.post);
 }
 puts("PASS: presets exclude effect bypass; independent enable metadata, v1/v2/v3 migration, save/init/recall preserve bypass, dirty SAVE LED including identical-value slot changes, two-row spaced dialog options");
}
int main(void){
 setbuf(stdout,NULL);palette_set(APP_PALETTE);now_ms=100;store_ready();store_len[OBJ_FX]=-1;reboot();
 /* An erased FX store starts globally bypassed, with individual effects ON.
  * Opening either editor must reflect this without persisting or enabling DSP. */
 assert(!gateway_fx_params.chain && gateway_fx_params.eq && gateway_fx_params.lim);
 assert(!gateway_fx_presets_saved.chain && gateway_fx_presets_saved.enabled==3);
 assert(!gateway_fx.latency_on && !gateway_fx.bank[gateway_fx.active].mask && !gateway_fx.bank[gateway_fx.active].lim);
 assert(lit_btn&(1u<<B_SEL));
 unsigned boot_writes=store_writes;
 tap(B_FX);assert(gateway_fx_limiter && (lit_btn&(1u<<B_SEL)));
 tap(B_FX);assert(!gateway_fx_limiter && (lit_btn&(1u<<B_SEL)));
 assert(!gateway_fx_params.chain && !gateway_fx_dirty && store_writes==boot_writes);
 puts("PASS: fresh FX store defaults to chain bypassed, EQ/limiter enabled, bright SEL in both editors, zero DSP delay and no implicit save");
 assert(sizeof(gateway_eq_preset_t)==48 && sizeof(gateway_lim_preset_t)==8 && sizeof(gateway_fx_presets_t)==456);
 for(unsigned i=0;i<8;i++)assert(!memcmp(&gateway_eq_work[i],&gateway_eq_work[0],48) && !memcmp(&gateway_lim_work[i],&gateway_lim_work[0],8));
 assert(!gateway_fx_selected[0] && !gateway_fx_selected[1]);title();
 /* Every EQ has its own volatile edit. Cycling applies it automatically,
  * leaves limiter/global state alone, and does not persist its parameters. */
 for(unsigned i=0;i<8;i++){
  assert(gateway_fx_selected[0]==i);now_ms+=200;turn(EN_K1,i+1);
  assert(gateway_fx_preset_dirty(0));title();turn(EN_PRESET,1);
 }
 assert(!gateway_fx_selected[0]);gateway_eq_preset_t working[8];memcpy(working,gateway_eq_work,sizeof working);
 for(unsigned i=0;i<8;i++){assert(!memcmp(&gateway_eq_work[i],&working[i],48));assert(gateway_fx_params.band[0].hz==working[i].band[0].hz);turn(EN_PRESET,1);}
 turn(EN_PRESET,-1);assert(gateway_fx_selected[0]==7);turn(EN_PRESET,INT_MIN);assert(gateway_fx_selected[0]==7);
 turn(EN_PRESET,INT_MAX);assert(gateway_fx_selected[0]==6);title();
 unsigned writes=store_writes;tap(B_SAVE);assert(gateway_fx_dialog==1 && store_writes==writes);dialog_text();tap(B_OCTDN);assert(!gateway_fx_dialog && gateway_fx_preset_dirty(0));
 save();assert(!gateway_fx_preset_dirty(0));title();
 gateway_eq_preset_t saved_eq=gateway_eq_work[6];
 tap(B_FX);assert(gateway_fx_limiter && !gateway_fx_selected[1]);
 for(unsigned i=0;i<8;i++){now_ms+=200;turn(EN_K1,i+1);assert(gateway_fx_preset_dirty(1));turn(EN_PRESET,1);}
 assert(!gateway_fx_selected[1] && gateway_fx_selected[0]==6);title();
 save();assert(!gateway_fx_preset_dirty(1));gateway_lim_preset_t saved_lim=gateway_lim_work[0];
 turn(EN_PRESET,4);now_ms+=750;app_loop(); /* Only selected positions/chain auto-save. */
 gateway_fx_presets_t persisted;assert(plat_store_load(OBJ_FX,&persisted,sizeof persisted)==456 && persisted.version==4);
 assert(persisted.selected[0]==6 && persisted.selected[1]==4);
 assert(!memcmp(&persisted.eq[6],&saved_eq,48) && !memcmp(&persisted.lim[0],&saved_lim,8));
 assert(persisted.eq[1].band[0].hz==80 && !persisted.lim[4].input);
 reboot();assert(gateway_fx_selected[0]==6 && gateway_fx_selected[1]==4);
 assert(!memcmp(&gateway_eq_work[6],&saved_eq,48) && gateway_eq_work[1].band[0].hz==80);
 assert(!memcmp(&gateway_lim_work[0],&saved_lim,8) && !gateway_lim_work[4].input && !gateway_fx_preset_dirty(0));
 /* Preset edits stay volatile; SEL bypass is independent autosaved metadata. */
 now_ms+=200;turn(EN_K1,2);tap(B_SEL);assert(gateway_fx_preset_dirty(0));now_ms+=6000;app_loop();
 reboot();assert(!memcmp(&gateway_eq_work[6],&saved_eq,48) && !gateway_fx_params.eq);
 /* Init is confirmed, persistent, per effect/slot; cancel is non-destructive. */
 writes=store_writes;hold_save();dialog_text();assert(store_writes==writes);tap(B_OCTDN);assert(!memcmp(&gateway_eq_work[6],&saved_eq,48));
 hold_save();tap(B_OCTUP);assert(!gateway_fx_dialog && !gateway_fx_preset_dirty(0) && gateway_fx_params.band[0].hz==80);
 reboot();assert(gateway_fx_selected[0]==6 && gateway_fx_params.band[0].hz==80);
 tap(B_FX);turn(EN_PRESET,-4);assert(gateway_fx_selected[1]==0 && gateway_fx_params.input==saved_lim.input);
 hold_save();tap(B_OCTUP);assert(!gateway_fx_params.input && gateway_fx_params.ceiling==-10 && gateway_fx_params.release==100);
 /* Failure does not commit edited slots; retry is explicit. Modal button and
  * encoder input cannot navigate, toggle FX, or start/cancel Bluetooth pairing. */
 turn(EN_PRESET,1);now_ms+=200;turn(EN_K3,-10);assert(gateway_fx_preset_dirty(1));tap(B_SAVE);store_fail=1;tap(B_OCTUP);
 assert(gateway_fx_dialog==1 && gateway_fx_notice==2 && gateway_fx_preset_dirty(1));
 dialog_text();
 writes=store_writes;store_fail=0;now_ms+=6000;app_loop();
 assert(gateway_fx_dialog==1 && gateway_fx_notice==2 && store_writes==writes && gateway_fx_dirty);
 unsigned muted=gateway_muted;tap(B_PLAY);assert(gateway_muted!=muted && gateway_fx_dialog==1);tap(B_PLAY);assert(gateway_muted==muted);
 uint8_t slot=gateway_fx_selected[1];turn(EN_PRESET,1);tap(B_HOME);tap(B_SEL);tap(B_FX);
 assert(gateway_fx_dialog==1 && gateway_screen==GATEWAY_FX && gateway_fx_selected[1]==slot);
 gateway_view_changed();assert(gateway_fx_dialog==1); /* Codec/view refresh cannot confirm or discard the dialog. */
 gateway_bt_mode=1;plat_bt_receiver_on(1);int pairing=plat_bt_receiver_pairing();
 store_fail=0;tap(B_OCTUP);assert(!gateway_fx_dialog && !gateway_fx_preset_dirty(1) && plat_bt_receiver_pairing()==pairing && !gateway_oct_tracking);
 assert(plat_store_load(OBJ_FX,&persisted,sizeof persisted)==456);
 for(unsigned bad=0;bad<12;bad++){
  gateway_fx_presets_t broken=persisted;
  if(bad==0)broken.selected[0]=8;if(bad==1)broken.selected[1]=255;
  if(bad==2)broken.chain=2;if(bad==3)broken.enabled=4;
  if(bad==4)broken.eq[7].slope[4]=4;if(bad==5)broken.lim[7].release=0;
  if(bad==6)broken.lim[4].reserved[1]=1;if(bad==7)broken.version=5;
  if(bad==8)broken.eq[4].reserved=1;if(bad==9)broken.lim[4].reserved[0]=1;
  if(bad==10){broken.version=3;broken.enabled=0;broken.eq[4].reserved=2;}
  if(bad==11){broken.version=3;broken.enabled=1;}
  assert(!plat_store_save(OBJ_FX,&broken,sizeof broken));reboot();
  assert(!gateway_fx_selected[0] && !gateway_fx_selected[1] && !gateway_fx_params.chain && !gateway_fx_preset_dirty(0));
  assert(gateway_fx_params.eq && gateway_fx_params.lim);
 }
 independent_bypass();assert(!cv_cuts && !cpu_work_depth);
 puts("PASS: 8 EQ + 8 limiter init slots, cyclic apply/independent selection, exact right-aligned white/yellow titles, volatile edits across navigation, selected-position-only autosave/reboot, explicit save/init/cancel/failure/retry, modal/Bluetooth isolation");
}
