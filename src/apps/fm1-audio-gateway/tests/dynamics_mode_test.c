/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();app_loop();}
static void turn(unsigned enc,int d){enc_acc[enc]=d;app_input_only();app_loop();}
static void row(unsigned r){turn(EN_SELECT,(int)r-gateway_settings_row);assert(gateway_settings_row==r);}
static void draft_and_apply(void)
{
    app_init();app_ui_init();app_loop();assert(gateway_dynamics_mode==FX_DYNAMICS_PEAK);
    assert(!gateway_fx.pending.dynamics && sizeof(gateway_settings_t)==32 && sizeof(fx_params_t)==58);
    tap(B_EDIT);row(GATEWAY_SET_DYNAMICS);assert(!strcmp(gateway_settings_value(GATEWAY_SET_DYNAMICS),"Peak Limiter"));
    unsigned writes=store_writes;turn(EN_ALGO,1);
    assert(gateway_dynamics_draft==FX_DYNAMICS_LM && !gateway_dynamics_mode && gateway_settings_unsaved());
    assert(!strcmp(gateway_settings_status(),"UNSAVED") && (lights_b&(1u<<B_SAVE)) && store_writes==writes);
    tap(B_HOME);tap(B_EDIT);assert(!gateway_dynamics_draft);row(GATEWAY_SET_DYNAMICS);turn(EN_ALGO,1);
    store_fail=1;tap(B_SAVE);assert(gateway_save_notice==2 && !gateway_dynamics_mode && !gateway_fx.pending.dynamics);
    store_fail=0;tap(B_SAVE);assert(gateway_dynamics_mode==FX_DYNAMICS_LM && gateway_fx.pending.dynamics==FX_DYNAMICS_LM);
    gateway_settings_t saved;assert(plat_store_load(OBJ_SETTINGS,&saved,sizeof saved)==32 && saved.version==23 && (saved.vectorscope_color&128));
    app_init();app_ui_init();assert(gateway_dynamics_mode==FX_DYNAMICS_LM && gateway_fx.pending.dynamics==FX_DYNAMICS_LM);
    /* Source-only post-FX recording has its own matching dynamics engine. */
    tap(B_EDIT);row(GATEWAY_SET_USB_OUT);turn(EN_ALGO,GATEWAY_USB_BT_FX);tap(B_SAVE);
    assert(gateway_usb_fx && gateway_usb_fx->pending.dynamics==FX_DYNAMICS_LM);
    row(GATEWAY_SET_DYNAMICS);turn(EN_ALGO,-1);tap(B_SAVE);
    assert(!gateway_dynamics_mode && !gateway_fx.pending.dynamics && !gateway_usb_fx->pending.dynamics);
    turn(EN_ALGO,1);tap(B_SAVE);
    puts("PASS: dynamics draft/discard/failed-save/commit/reboot; unchanged settings size, extended independent preset banks, both DSP routes apply selected mode atomically");
}
static void controls_and_presets(void)
{
    tap(B_FX);tap(B_FX);assert(gateway_fx_limiter);
    assert(text_w(&FONT_T,"FX: DYNAMICS: LM-Maximizer")+text_w(&FONT_T,"Preset 8")<225);
    uint16_t actual[240*16];memcpy(actual,fb+18*240,sizeof actual);
    cv_begin(240,16,C_BLACK);cv_text(5,1,&FONT_T,"FX: DYNAMICS: LM-Maximizer",C_WHITE);
    cv_text(235-text_w(&FONT_T,"Preset 1"),1,&FONT_T,"Preset 1",C_WHITE);cv_blit(0,18);
    assert(!memcmp(actual,fb+18*240,sizeof actual));
    unsigned release=gateway_fx_params.release;int ceiling=gateway_fx_params.ceiling;
    turn(EN_K3,INT_MAX);assert(gateway_fx_params.release==release && gateway_fx_params.ceiling==ceiling && !gateway_fx_preset_dirty(1));
    turn(EN_K1,INT_MAX);assert(gateway_fx_params.input==300);
    turn(EN_K2,-2);assert(gateway_fx_params.ceiling<ceiling && gateway_fx_params.release==release && gateway_fx_preset_dirty(1));
    tap(B_SAVE);tap(B_OCTUP);assert(!gateway_fx_preset_dirty(1));
    gateway_fx_presets_t bank;assert(plat_store_load(OBJ_FX,&bank,sizeof bank)==522 && bank.version==5);
    assert(bank.lm[0].input==300 && !bank.lim[0].input && gateway_fx_presets_valid(&bank));
    tap(B_SEL);assert(!gateway_fx_params.lim);turn(EN_PRESET,1);assert(!gateway_fx_params.lim && gateway_dynamics_mode==FX_DYNAMICS_LM);
    held_btn=1u<<B_SAVE;app_input_only();now_ms+=1100;app_input_only();held_btn=0;app_input_only();
    assert(gateway_fx_dialog==2);tap(B_OCTUP);assert(gateway_dynamics_mode==FX_DYNAMICS_LM && !gateway_fx_params.lim);
    tap(B_EDIT);row(GATEWAY_SET_DYNAMICS);turn(EN_ALGO,-1);tap(B_SAVE);tap(B_FX);
    turn(EN_K2,1);assert(gateway_fx_params.release>release);
    ceiling=gateway_fx_params.ceiling;turn(EN_K3,-1);assert(gateway_fx_params.ceiling<ceiling);
    turn(EN_K1,INT_MAX);assert(gateway_fx_params.input==300);
    puts("PASS: Dynamics title/preset fit, automatic-release knob gate, input/ceiling presets unchanged, init/recall preserves mode and bypass; legacy manual release restored");
}
static void migration(void)
{
    for(unsigned mode=0;mode<2;mode++)for(int octave=-3;octave<=3;octave++){
        gateway_settings_t s=gateway_settings();s.zoom=9;s.vectorscope_color=GATEWAY_WAVE_ORANGE|8u|(unsigned)(octave+3)<<4|mode<<7;
        for(unsigned e=0;e<NE;e++)s.midi_position[e]=17+e;
        assert(!plat_store_save(OBJ_SETTINGS,&s,32));app_init();
        assert(gateway_zoom==9 && gateway_dynamics_mode==mode && gateway_midi_resume_octave==octave);
        assert(gateway_vectorscope_color==GATEWAY_WAVE_ORANGE && gateway_vectorscope_auto_gain && gateway_midi_resume[2]==19);
    }
    for(unsigned version=19;version<=22;version++){
        gateway_settings_t s=gateway_settings();s.version=version;s.zoom=9;
        s.vectorscope_color=version==19?0:version==20?1:version==21?9:9|80;
        assert(!plat_store_save(OBJ_SETTINGS,&s,32));app_init();assert(gateway_zoom==9 && !gateway_dynamics_mode);
        if(version==22)assert(gateway_midi_resume_octave==2);
        s.vectorscope_color|=128;assert(!plat_store_save(OBJ_SETTINGS,&s,32));app_init();assert(gateway_zoom==6 && !gateway_dynamics_mode);
    }
    gateway_settings_t s=gateway_settings();s.zoom=9;s.vectorscope_color=128|112|1;
    assert(!plat_store_save(OBJ_SETTINGS,&s,32));app_init();assert(gateway_zoom==6);
    assert(!cpu_work_depth && !cv_cuts);
    puts("PASS: v19-v22 migrate to Peak Limiter, old reserved-bit validation retained; v23 mode/color/scale/all MIDI octaves coexist, invalid octave rejected");
}
static void choose_mode(unsigned mode)
{
    tap(B_EDIT);row(GATEWAY_SET_DYNAMICS);turn(EN_ALGO,(int)mode-gateway_dynamics_draft);
    tap(B_SAVE);assert(gateway_dynamics_mode==mode);tap(B_FX);assert(gateway_fx_limiter);
}
static void edit(int input,int ceiling,unsigned release)
{
    fx_params_t p=gateway_fx_params;p.input=input;p.ceiling=ceiling;p.release=release;gateway_fx_publish(&p);
}
static void save_slot(void){tap(B_SAVE);assert(gateway_fx_dialog==1);tap(B_OCTUP);assert(!gateway_fx_dialog && !gateway_fx_preset_dirty(1));}
static void independent_banks(void)
{
    store_len[OBJ_FX]=store_len[OBJ_SETTINGS]=-1;app_init();app_ui_init();tap(B_FX);tap(B_FX);
    edit(120,-30,247);save_slot();
    turn(EN_PRESET,3);edit(60,-50,350);assert(gateway_fx_preset_dirty(1));
    choose_mode(FX_DYNAMICS_LM);
    assert(!gateway_fx_selected[2] && !gateway_fx_params.input && gateway_fx_params.ceiling==-10 && gateway_fx_params.lim);
    edit(180,-20,100);save_slot();
    assert(gateway_fx_presets_saved.lim[0].input==120 && gateway_fx_presets_saved.lim[0].release==247);
    turn(EN_PRESET,5);edit(90,-70,100);tap(B_SEL);assert(!gateway_fx_params.lim);
    choose_mode(FX_DYNAMICS_PEAK);
    assert(gateway_fx_selected[1]==3 && gateway_fx_params.input==60 && gateway_fx_params.release==350 && !gateway_fx_params.lim);
    assert(gateway_fx_preset_dirty(1) && (lights_b&(1u<<B_SAVE)));
    /* A failed Settings SAVE cannot transfer ownership to the other bank. */
    tap(B_EDIT);row(GATEWAY_SET_DYNAMICS);turn(EN_ALGO,1);store_fail=1;tap(B_SAVE);store_fail=0;
    assert(!gateway_dynamics_mode && gateway_fx_params.input==60 && gateway_lm_work[5].input==90);
    tap(B_HOME);choose_mode(FX_DYNAMICS_LM);
    assert(gateway_fx_selected[2]==5 && gateway_fx_params.input==90 && !gateway_fx_params.lim && gateway_fx_preset_dirty(1));
    held_btn=1u<<B_SAVE;app_input_only();now_ms+=1000;app_input_only();held_btn=0;app_input_only();
    assert(gateway_fx_dialog==2);tap(B_OCTUP);
    assert(!gateway_fx_params.input && !gateway_fx_preset_dirty(1) && gateway_lim_work[3].input==60 && !gateway_fx_params.lim);
    turn(EN_PRESET,1);edit(200,-25,100);save_slot();edit(201,-25,100);
    choose_mode(FX_DYNAMICS_PEAK);assert(gateway_fx_params.input==60 && gateway_fx_preset_dirty(1));
    tap(B_SEL);assert(gateway_fx_params.lim);choose_mode(FX_DYNAMICS_LM);assert(gateway_fx_params.lim && gateway_fx_params.input==201);
    tap(B_SEL);choose_mode(FX_DYNAMICS_PEAK);now_ms+=800;app_loop();
    gateway_fx_presets_t saved;assert(plat_store_load(OBJ_FX,&saved,sizeof saved)==522);
    assert(saved.selected[1]==3 && saved.lm_selected==6 && saved.lim[3].input==0 && saved.lm[6].input==200 && !(saved.enabled&2));
    app_init();app_ui_init();assert(!gateway_dynamics_mode && gateway_fx_selected[1]==3 && gateway_fx_selected[2]==6 && !gateway_fx_params.input && !gateway_fx_params.lim);
    tap(B_FX);tap(B_FX);choose_mode(FX_DYNAMICS_LM);
    assert(gateway_fx_params.input==200 && !gateway_fx_params.lim && !gateway_fx_preset_dirty(1));
    assert(gateway_fx.pending.dynamics==FX_DYNAMICS_LM && gateway_fx.pending.input==1); /* Shared bypass remains off. */
    puts("PASS: 8 independent presets and selection per dynamics mode; volatile edits, save/init, failure atomicity, metadata-only autosave and reboot restore; one shared bypass");
}
static void preset_migration(void)
{
    for(unsigned version=3;version<=4;version++)for(unsigned mode=0;mode<2;mode++){
        gateway_fx_presets_t old=gateway_fx_presets_saved;old.version=version;old.selected[0]=2;old.selected[1]=5;old.chain=1;old.enabled=version==3?0:2;
        for(unsigned i=0;i<8;i++){
            old.eq[i].reserved=version==3?i%2:0;old.lim[i].reserved[0]=version==3?i%2:0;
            old.lim[i].input=10*i;old.lim[i].ceiling=-10-i;old.lim[i].release=100+i;
        }
        assert(!plat_store_save(OBJ_FX,&old,456));
        gateway_settings_t s=gateway_settings();s.vectorscope_color=(s.vectorscope_color&127u)|(mode<<7);
        assert(!plat_store_save(OBJ_SETTINGS,&s,sizeof s));unsigned writes=store_writes;app_init();
        assert(gateway_dynamics_mode==mode && gateway_fx_selected[1]==5 && gateway_fx_selected[2]==5);
        assert(!memcmp(gateway_lim_work,gateway_lm_work,sizeof gateway_lm_work) && gateway_fx_params.input==50 && gateway_fx_params.release==105);
        assert(!gateway_fx_params.eq && gateway_fx_params.lim && gateway_fx_params.chain && store_writes==writes);
        assert(gateway_fx_presets_saved.version==5 && gateway_fx_presets_valid(&gateway_fx_presets_saved));
    }
    puts("PASS: real 456-byte AFX v3/v4 banks migrate to both independent banks with every saved parameter/selection and shared bypass retained, no boot write");
}
int main(void){setbuf(stdout,NULL);palette_set(APP_PALETTE);now_ms=100;draft_and_apply();controls_and_presets();migration();independent_banks();preset_migration();}
