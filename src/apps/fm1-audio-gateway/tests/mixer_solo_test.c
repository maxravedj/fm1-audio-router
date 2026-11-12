/* SPDX-License-Identifier: MPL-2.0
 * Actual app/BSP controls, Q16 fades, settings migration and mixer pixels. */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <math.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void key(unsigned k){held_keys=1u<<k;app_input_only();held_keys=0;app_input_only();}
static void turn(unsigned e,int d){enc_acc[e]=d;app_input_only();}
static unsigned cut(unsigned m,unsigned s,unsigned i){
    unsigned group=i<2?3:12;
    return !!((m&(1u<<i)) || ((s&group) && !(s&(1u<<i))));
}
static void setup(unsigned fade,unsigned m,unsigned s){
    gateway_mix_mutes=m&7;gateway_muted=!!(m&8);gateway_mix_solos=s;gateway_fade_time=fade;
    gateway_mix_db[0]=gateway_mix_db[1]=gateway_mix_db[2]=0;
    gateway_mixer_init();plat_master_mute_init(cut(m,s,3));
}
static void truth(void){
    for(unsigned m=0;m<16;m++)for(unsigned s=0;s<16;s++){
        setup(0,m,s);
        for(unsigned i=0;i<4;i++){
            unsigned blocked=cut(m,s,i);assert(gateway_mixer_cut(i)==blocked);
            int32_t lr[2]={1000000,-500000};
            if(i<3){app_input_fader_process(i,lr,1);app_input_fader_idle(i,1);}
            else master_fade_apply(lr,1);
            assert(lr[0]==(blocked?0:1000000) && lr[1]==(blocked?0:-500000));
        }
    }
    puts("PASS: all 256 mute/solo combinations, independent USB/BT and OUT/MASTER solo groups, multiple solos and mute precedence, exact OFF/unity stereo PCM");
}
static void fades(void){
    for(unsigned fade=0;fade<6;fade++)for(unsigned direction=0;direction<2;direction++){
        setup(fade,direction?15:0,0);
        gateway_mix_mutes=direction?0:7;gateway_muted=!direction;gateway_mixer_publish();
        unsigned n=gateway_fade_ms[fade]*441/10,count=n?n:1;
        for(unsigned j=0;j<count;j++){
            unsigned distance=n?(unsigned)((uint64_t)65536*(j+1)/n):65536;
            unsigned q=direction?distance:65536-distance;
            for(unsigned i=0;i<4;i++){
                int32_t lr[2]={1000000,-500000};
                if(i<3){app_input_fader_process(i,lr,1);app_input_fader_idle(i,1);assert(gateway_gate[i].gain==q);}
                else{master_fade_apply(lr,1);assert(master_fade_gain==q);}
                assert(abs(lr[0]-(int32_t)(1000000.0*q/65536))<=1);
                assert(abs(lr[1]-(int32_t)(-500000.0*q/65536))<=1);
            }
        }
    }
    setup(4,0,0);gateway_mix_mutes=7;gateway_muted=1;gateway_mixer_publish();
    for(unsigned j=0;j<77;j++){
        int32_t lr[2]={1000000,-500000};app_input_fader_process(0,lr,1);app_input_fader_idle(0,1);master_fade_apply(lr,1);
    }
    /* Absent sources catch up in a batch, without a reconnect fade/leak. */
    app_input_fader_idle(1,77);app_input_fader_idle(2,77);
    assert(gateway_gate[0].gain==gateway_gate[1].gain && gateway_gate[0].gain==gateway_gate[2].gain);
    unsigned before=gateway_gate[0].gain;
    gateway_mix_mutes=0;gateway_muted=0;gateway_mixer_publish();
    int32_t lr[2]={1000000,-500000};app_input_fader_process(0,lr,1);app_input_fader_idle(0,1);master_fade_apply(lr,1);
    assert(gateway_gate[0].gain>=before && gateway_gate[0].gain-before<=3 && master_fade_gain==gateway_gate[0].gain);
    gateway_mix_mutes=7;gateway_muted=1;gateway_fade_time=1;gateway_mixer_publish();before=gateway_gate[0].gain;
    app_input_fader_process(0,lr,1);app_input_fader_idle(0,1);master_fade_apply(lr,1);
    assert(before-gateway_gate[0].gain>=148 && before-gateway_gate[0].gain<=149 && master_fade_gain==gateway_gate[0].gain);
    app_input_fader_idle(1,100000);app_input_fader_idle(2,100000);
    assert(!gateway_gate[1].gain && !gateway_gate[2].gain);
    puts("PASS: all six exact fade durations both directions, stereo gain oracle, continuous reversal/duration changes, inactive BT/OUT catch-up, no callback allocation");
}
static void controls(void){
    setup(3,0,0);held_keys=held_btn=0;gateway_keys=0;
    gateway_screen=GATEWAY_MIXER;gateway_view_changed();app_loop();
    const unsigned mute_keys[]={0,2,4,6},solo_keys[]={12,14,16,18};unsigned all=0;
    for(unsigned i=0;i<4;i++)all|=1u<<mute_keys[i]|1u<<solo_keys[i];
    assert((dim_keys&all)==all && !(lit_keys&all));
    for(unsigned i=0;i<4;i++){
        held_keys=1u<<mute_keys[i];app_input_only();assert(gateway_mixer_muted(i));
        app_input_only();assert(gateway_mixer_muted(i)); /* held does not retrigger */
        held_keys=0;app_input_only();assert(lit_keys&(1u<<mute_keys[i]));
        key(solo_keys[i]);assert(gateway_mix_solos&(1u<<i));assert(lit_keys&(1u<<solo_keys[i]));
    }
    assert(gateway_muted && (lights_b&(1u<<B_PLAY)) && !(lights_b&LED_PLAY_GREEN));
    tap(B_PLAY);assert(!gateway_muted && !gateway_mixer_muted(3) && (dim_keys&(1u<<6)) && (lights_b&LED_PLAY_GREEN));
    key(6);assert(gateway_muted && (lights_b&(1u<<B_PLAY)));key(6);assert(!gateway_muted);
    tap(B_HOME);assert(!lit_keys && !dim_keys);
    unsigned m=gateway_mix_mutes,s=gateway_mix_solos;key(0);key(12);assert(m==gateway_mix_mutes && s==gateway_mix_solos);
    held_keys=1u;app_input_only();tap(B_GLO);assert(gateway_mix_mutes==m);held_keys=0;app_input_only();
    key(0);assert(gateway_mix_mutes==(m^1u));
    puts("PASS: F3/G3/A3/B3 mute, F4/G4/A4/B4 solo, edge-only Mixer scope, dim/full LEDs and two-way PLAY/MASTER M synchronization");
}
static void settings(void){
    setup(3,5,10);gateway_screen=GATEWAY_MIXER;gateway_view_changed();
    gateway_settings_changed();now_ms+=750;app_loop();
    gateway_settings_t s;assert(sizeof s==32 && plat_store_load(OBJ_SETTINGS,&s,sizeof s)==sizeof s && s.version==GATEWAY_SETTINGS_VERSION);
    assert(s.mixer_controls==(5|10<<3) && s.fade_time==3);
    app_init();app_ui_init();assert(gateway_mix_mutes==5 && gateway_mix_solos==10 && gateway_fade_time==3);
    assert(!gateway_gate[0].gain && !gateway_gate[2].gain && master_fade_gain==65536);
    tap(B_EDIT);turn(EN_SELECT,GATEWAY_SET_FADE-gateway_settings_row);assert(gateway_settings_row==GATEWAY_SET_FADE);
    turn(EN_ALGO,-100);assert(!strcmp(gateway_settings_value(GATEWAY_SET_FADE),"OFF") && gateway_fade_time==3);
    for(unsigned i=0;i<6;i++){
        if(i)turn(EN_ALGO,1);assert(gateway_fade_draft==i);
        store_fail=1;uint32_t config=gateway_gate_config;tap(B_SAVE);
        assert(gateway_save_notice==2 && gateway_gate_config==config);
        store_fail=0;tap(B_SAVE);assert(gateway_fade_time==i && master_fade_frames==gateway_fade_ms[i]*441u/10u);
        gateway_settings_t saved;assert(plat_store_load(OBJ_SETTINGS,&saved,sizeof saved)==sizeof saved && saved.fade_time==i);
    }
    turn(EN_ALGO,-1);assert(gateway_fade_draft==4 && gateway_fade_time==5);
    tap(B_HOME);tap(B_EDIT);assert(gateway_fade_draft==5 && gateway_fade_time==5);
    /* Legacy length must remain 22, and retain finite/OFF trim semantics. */
    s=gateway_settings();s.version=14;s.bt_reserved[0]=0;s.input_db[0]=-1000;s.input_db[1]=GATEWAY_GAIN_OFF;s.muted=1;s.zoom=9;
    assert(!plat_store_save(OBJ_SETTINGS,&s,22));app_init();app_ui_init();
    assert(gateway_zoom==9 && gateway_muted && !master_fade_gain && !gateway_mix_mutes && !gateway_mix_solos && gateway_fade_time==2);
    assert(gateway_mix_db[0]==-1000 && gateway_mix_db[1]==GATEWAY_GAIN_OFF);
    for(unsigned bad=0;bad<4;bad++){
        s=gateway_settings();s.zoom=9;
        if(bad==0)s.fade_time=6;
        if(bad==1)s.mixer_controls=128;
        if(bad==2)s.version=14;s.bt_reserved[0]=0;
        unsigned len=bad==3?22:sizeof s;
        assert(!plat_store_save(OBJ_SETTINGS,&s,len));app_init();app_ui_init();
        assert(gateway_zoom==6 && !gateway_muted && !gateway_mix_mutes && !gateway_mix_solos && gateway_fade_time==2);
    }
    puts("PASS: current controls autosave/reboot without startup leak, fade draft/SAVE/failure/discard, v14 migration and strict malformed length/flags/fade validation");
}
static void pixels(void){
    setup(3,0,0);gateway_screen=GATEWAY_MIXER;gateway_view_changed();
    for(unsigned m=0;m<16;m++)for(unsigned s=0;s<16;s++){
        gateway_mix_mutes=m&7;gateway_muted=!!(m&8);gateway_mix_solos=s;gateway_mixer_publish();
        for(unsigned i=0;i<8;i++)gateway_meter[i]=(gateway_meter_t){600,600,750,1,0,0};
        gateway_meter_ms=now_ms;gateway_mixer_draw(m==0 && s==0);
        for(unsigned g=0;g<4;g++){
            unsigned base=20+55*g;int off=cut(m,s,g);
            assert(fb[208*240+base]==swap16(off?RGB(90,90,90):gateway_meter_color(20)));
            assert(fb[60*240+base]==swap16(off?RGB(45,45,45):RGB(255,48,48)));
            for(unsigned solo=0;solo<2;solo++){
                unsigned x=base+37,y=solo?197:180,on=(solo?s:m)&(1u<<g);
                uint16_t c=solo?RGB(255,212,40):RGB(255,48,48);
                assert(fb[y*240+x]==swap16(c));
                assert(fb[(y+1)*240+x+1]==swap16(on?c:C_BLACK));
            }
        }
    }
    for(unsigned i=0;i<8;i++)gateway_meter[i]=(gateway_meter_t){0};
    gateway_mixer_draw(0);
    for(unsigned g=0;g<4;g++)assert(fb[208*240+20+55*g]==swap16(RGB(20,20,20)));
    setup(3,15,5);gateway_screen=GATEWAY_MAIN;gateway_mode=GATEWAY_MODE_METERS;gateway_view_changed();
    for(unsigned i=0;i<8;i++)gateway_meter[i]=(gateway_meter_t){600,600,750,0,0,0};
    gateway_draw_meter(1);
    for(unsigned g=0;g<4;g++)assert(fb[204*240+26+54*g]==swap16(gateway_meter_color(19)));
    puts("PASS: all Mixer mute/solo gray/active meter pixels, red/yellow outline/fill switches, HOME meter colors unaffected by mixer-only gray styling");
}
int main(void){
    setbuf(stdout,NULL);palette_set(APP_PALETTE);now_ms=1000;app_init();app_ui_init();
    truth();fades();controls();settings();pixels();assert(!cv_cuts && !cpu_work_depth);
}
