/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static const unsigned cc[NE]={20,21,26,22,23,24,25};
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();app_loop();}
static void turn_after(unsigned e,int d,uint32_t elapsed){now_ms+=elapsed;enc_acc[e]=d;app_input_only();app_loop();}
static void turn(unsigned e,int d){unsigned count=d< -127 || d>127?127u:d<0?(unsigned)-d:(unsigned)d;turn_after(e,d,100u*count);}
static void select_row(unsigned row){turn(EN_SELECT,(int)row-gateway_settings_row);}
static void key(unsigned k,int on){if(on)held_keys|=1u<<k;else held_keys&=~(1u<<k);app_input_only();app_loop();}
static void drain(void){for(unsigned i=0;i<8;i++)gateway_midi_service();}
static void message(unsigned i,unsigned cin,unsigned st,unsigned d1,unsigned d2)
{
    assert(i<n_out && out_to[i]==TO_USB);
    assert(out_pkt[i]==(cin | st<<8 | d1<<16 | d2<<24));
}
static void enable(unsigned channel,int highlight)
{
    tap(B_EDIT);select_row(GATEWAY_SET_MIDI);turn(EN_ALGO,1);
    select_row(GATEWAY_SET_MIDI_CHANNEL);turn(EN_ALGO,(int)channel-1-gateway_midi_channel_draft);
    select_row(GATEWAY_SET_MIDI_C);turn(EN_ALGO,highlight-gateway_midi_c_draft);
    assert(!gateway_midi_mode && gateway_settings_unsaved() && lights_b&(1u<<B_SAVE));
    tap(B_SAVE);assert(gateway_midi_mode && !gateway_midi_active && !gateway_settings_unsaved());
    assert(lights_db&(1u<<B_REC));
}
static void settings(void)
{
    app_init();app_ui_init();app_loop();
    assert(sizeof(gateway_settings_t)==32 && !gateway_midi_mode && !gateway_midi_active);
    assert(!(lights_b&(1u<<B_REC)) && !(lights_db&(1u<<B_REC)));
    tap(B_REC);assert(!gateway_midi_active);
    enable(16,1);
    gateway_settings_t s;assert(plat_store_load(OBJ_SETTINGS,&s,sizeof s)==32 && s.version==19);
    assert((s.reserved&96u)==96u && s.bt_reserved[2]>>4==15u);
    unsigned writes=store_writes;
    app_init();app_ui_init();assert(gateway_midi_mode && gateway_midi_c && gateway_midi_channel==15 && !gateway_midi_active);
    assert(store_writes==writes);
    /* Draft-only edits, failure and explicit retry preserve applied MIDI. */
    tap(B_EDIT);select_row(GATEWAY_SET_MIDI_CHANNEL);turn(EN_ALGO,INT_MIN);
    assert(!strcmp(gateway_settings_value(GATEWAY_SET_MIDI_CHANNEL),"1") && gateway_midi_channel==15);
    store_fail=1;tap(B_SAVE);assert(gateway_midi_channel==15 && gateway_settings_unsaved());
    store_fail=0;tap(B_SAVE);assert(!gateway_midi_channel && !gateway_settings_unsaved());
    select_row(GATEWAY_SET_MIDI_C);turn(EN_ALGO,-1);tap(B_HOME);tap(B_EDIT);
    assert(gateway_midi_c_draft && !gateway_settings_unsaved());
    /* v17 preserves the old routes/colors and safely defaults new settings. */
    s.version=17;s.reserved=1u|(6u<<2);s.bt_reserved[2]=GATEWAY_WAVE_ORANGE<<1;
    plat_store_save(OBJ_SETTINGS,&s,24);app_init();app_ui_init();
    assert(!gateway_midi_mode && !gateway_midi_c && !gateway_midi_channel);
    assert(gateway_usb_out_mode==6 && gateway_wave_color==GATEWAY_WAVE_ORANGE);
    for(unsigned channel=0;channel<16;channel++){
        gateway_midi_mode=gateway_midi_c=1;gateway_midi_channel=channel;
        s=gateway_settings();plat_store_save(OBJ_SETTINGS,&s,sizeof s);app_init();app_ui_init();
        assert(gateway_midi_channel==channel && gateway_midi_mode && gateway_midi_c && !gateway_midi_active);
        assert(gateway_usb_out_mode==6 && gateway_wave_color==GATEWAY_WAVE_ORANGE);
    }
    s=gateway_settings();s.reserved|=128u;plat_store_save(OBJ_SETTINGS,&s,sizeof s);app_init();app_ui_init();
    assert(!gateway_midi_mode && !gateway_midi_channel);
    s=gateway_settings();s.bt_reserved[2]=15;plat_store_save(OBJ_SETTINGS,&s,sizeof s);app_init();app_ui_init();
    assert(!gateway_midi_mode && gateway_wave_color==GATEWAY_WAVE_GREEN);
    store_len[OBJ_SETTINGS]=-1;app_init();app_ui_init();
    puts("PASS: MIDI draft/SAVE/failure/discard, all 16 channels, 32-byte v19 payload, v17 migration and strict flags/color bounds; active mode never restored");
}
static void controls(void)
{
    enable(3,1);unsigned writes=store_writes;
    tap(B_HOME);
    /* A reconnect while idle must not discard the first new encoder turn. */
    usb_links++;held_btn=1u<<B_REC;enc_acc[EN_K1]=1;app_input_only();
    message(0,11,0xb2,22,65);n_out=0;
    for(unsigned i=0;i<4;i++)app_input_only();
    assert(gateway_midi_active && lights_b&(1u<<B_REC));
    held_btn=0;app_input_only();
    assert(lit_keys==GATEWAY_MIDI_CS && dim_keys==(GATEWAY_MIDI_KEYS&~GATEWAY_MIDI_CS));
    assert(!n_out); /* Highlighting C never sends notes. */
    turn(EN_K1,-1);message(0,11,0xb2,22,64);n_out=0;
    for(unsigned k=0;k<27;k++){
        n_out=0;key(k,1);assert(lit_keys==(GATEWAY_MIDI_CS|(1u<<k)));
        assert(dim_keys==(GATEWAY_MIDI_KEYS&~lit_keys));
        message(0,9,0x92,53+k,127);key(k,0);message(1,8,0x82,53+k,0);assert(n_out==2);
        assert(lit_keys==GATEWAY_MIDI_CS);
    }
    gateway_midi_c=0;gateway_lights();assert(!lit_keys && dim_keys==GATEWAY_MIDI_KEYS);
    key(7,1);assert(lit_keys==(1u<<7));key(7,0);assert(!lit_keys);
    gateway_midi_c=1;gateway_lights();
    gateway_settings_t before=gateway_settings();fx_params_t fx_before=gateway_fx_params;
    for(unsigned e=0;e<NE;e++){
        assert(gateway_midi_values[e]==64);
        static const int step[]={1,1,-1,-2,INT_MAX,-1,INT_MIN,1,63};
        static const unsigned value[]={65,66,65,63,127,126,0,1,64};
        n_out=0;
        for(unsigned j=0;j<9;j++){turn(e,step[j]);message(j,11,0xb2,cc[e],value[j]);}
        assert(n_out==9);
        turn(e,INT_MIN);n_out=0;turn(e,-1);assert(!n_out && !gateway_midi_values[e]);
        for(unsigned v=1;v<128;v++){turn(e,1);message(0,11,0xb2,cc[e],v);n_out=0;}
        turn(e,1);assert(!n_out && gateway_midi_values[e]==127);
        for(int v=126;v>=0;v--){turn(e,-1);message(0,11,0xb2,cc[e],(unsigned)v);n_out=0;}
        turn(e,64);message(0,11,0xb2,cc[e],64);
    }
    gateway_settings_t after=gateway_settings();assert(!memcmp(&before,&after,sizeof before));
    assert(!memcmp(&fx_before,&gateway_fx_params,sizeof fx_before) && store_writes==writes);
    n_out=0;master=1;app_input_only();master=4096;app_input_only();assert(!n_out);
    /* The same routing and REC LEDs apply on every screen; Mixer keys do not mute/solo. */
    tap(B_GLO);n_out=0;unsigned controls=gateway_mix_mutes|(gateway_mix_solos<<4);
    key(0,1);key(0,0);assert(n_out==2 && controls==(gateway_mix_mutes|(gateway_mix_solos<<4)));
    int16_t gain=gateway_mix_db[0];turn(EN_K1,1);assert(gateway_mix_db[0]==gain);
    tap(B_EDIT);unsigned row=gateway_settings_row;turn(EN_SELECT,1);assert(gateway_settings_row==row);
    gateway_settings_row=GATEWAY_SET_CODEC;tap(B_SEL);assert(gateway_codec_dialog);
    n_out=0;turn(EN_SELECT,1);message(0,11,0xb2,20,66);assert(!gateway_codec_row);
    tap(B_OCTDN);assert(!gateway_codec_dialog && gateway_midi_active && !gateway_midi_octave);
    tap(B_FX);fx_before=gateway_fx_params;turn(EN_K1,1);assert(!memcmp(&fx_before,&gateway_fx_params,sizeof fx_before));
    /* Dialogs don't consume encoders or REC ahead of controller routing. */
    gateway_fx_dialog=1;gateway_lights();n_out=0;turn(EN_ALGO,-1);message(0,11,0xb2,21,63);
    uint8_t positions[NE];memcpy(positions,gateway_midi_values,sizeof positions);
    tap(B_REC);assert(!gateway_midi_active && lights_db&(1u<<B_REC));
    tap(B_REC);assert(gateway_midi_active && lights_b&(1u<<B_REC));
    assert(!memcmp(positions,gateway_midi_values,sizeof positions) && !gateway_midi_pending);
    tap(B_OCTDN);assert(!gateway_midi_octave);tap(B_HOME);
    key(7,1);n_out=0;tap(B_REC);message(0,8,0x82,60,0);assert(!gateway_midi_active && !gateway_midi_sent);
    assert(!lit_keys && !dim_keys);key(7,0);
    n_out=0;key(1,1);key(1,0);assert(!n_out);tap(B_HOME);unsigned zoom=gateway_zoom;turn(EN_K1,1);assert(gateway_zoom==zoom+1);
    puts("PASS: all F3-G5 notes/channel/velocity, highlighted C stays bright when pressed, normal C brightness without highlight, seven independent absolute CCs through all 128 values both directions at slow speed, end clamps/extreme deltas, positions retained across REC, UI isolation and unchanged MASTER");
}
static void pressure(void)
{
    tap(B_REC);midi_try_limited=1;midi_try_slots=0;n_out=0;
    held_keys=GATEWAY_MIDI_KEYS;app_input_only();assert(!n_out && !gateway_midi_sent);
    midi_try_slots=64;drain();assert(n_out==27 && gateway_midi_sent==GATEWAY_MIDI_KEYS);
    midi_try_slots=0;tap(B_REC);assert(!gateway_midi_active && gateway_midi_sent==GATEWAY_MIDI_KEYS);
    midi_try_slots=3;gateway_midi_service();assert(n_out==30 && gateway_midi_sent);
    midi_try_slots=64;drain();assert(n_out==54 && !gateway_midi_sent);
    for(unsigned k=0;k<27;k++)message(27+k,8,0x82,53+k,0);
    held_keys=0;app_input_only();tap(B_REC);n_out=0;midi_try_slots=0;
    turn(EN_K4,INT_MAX);assert(gateway_midi_values[EN_K4]==127);
    turn(EN_K4,INT_MIN);assert(!gateway_midi_values[EN_K4]);
    turn(EN_K4,9);midi_try_slots=1;gateway_midi_service();message(0,11,0xb2,25,9);
    assert(!gateway_midi_pending);
    midi_try_slots=0;n_out=0;
    for(unsigned e=0;e<NE;e++){turn(e,INT_MIN);turn(e,(int)e+1);turn(e,10);turn(e,-3);}
    assert(!n_out && gateway_midi_pending==(1u<<NE)-1u);
    midi_try_slots=3;gateway_midi_service();assert(n_out==3 && gateway_midi_pending);
    midi_try_slots=64;drain();assert(n_out==NE && !gateway_midi_pending);
    for(unsigned e=0;e<NE;e++)message(e,11,0xb2,cc[e],e+8);
    midi_try_slots=0;turn(EN_K3,1);assert(gateway_midi_pending);
    unsigned position=gateway_midi_values[EN_K3];tap(B_REC);
    assert(!gateway_midi_active && !gateway_midi_pending && gateway_midi_values[EN_K3]==position);
    midi_try_slots=64;tap(B_REC);n_out=0;turn(EN_K3,1);message(0,11,0xb2,24,position+1);
    /* A changed channel first releases every old-channel note, then restarts. */
    midi_try_slots=64;key(2,1);n_out=0;gateway_midi_channel=15;gateway_midi_service();
    message(0,8,0x82,55,0);message(1,9,0x9f,55,127);assert(n_out==2);
    midi_try_connected=0;n_out=0;turn(EN_K2,8);assert(!n_out);
    usb_links++;midi_try_connected=1;midi_try_slots=64;gateway_midi_service();
    assert(n_out==1 && !gateway_midi_pending && gateway_midi_values[EN_K2]==20);message(0,9,0x9f,55,127);
    n_out=0;turn(EN_K2,1);message(0,11,0xbf,23,21);
    n_out=0;app_update_begin();assert(!gateway_midi_active && !gateway_midi_sent);message(0,8,0x8f,55,0);
    held_keys=0;midi_try_limited=0;
    assert(!cv_cuts && !cpu_work_depth);
    puts("PASS: full/disconnected USB retains note-offs, bounded partial sends, latest absolute value coalescing for all seven encoders, exit/reconnect drops pending CC without resetting positions, old-channel releases and updater cleanup");
}
static void acceleration(void)
{
    tap(B_REC);assert(gateway_midi_active);
    for(unsigned e=0;e<NE;e++){
        gateway_midi_values[e]=64;gateway_midi_turn_dir[e]=0;n_out=0;
        turn_after(e,1,0);message(0,11,0xbf,cc[e],65); /* First turn stays fine. */
        turn_after(e,1,80);message(1,11,0xbf,cc[e],66);
        turn_after(e,1,79);message(2,11,0xbf,cc[e],68);
        turn_after(e,1,35);message(3,11,0xbf,cc[e],70);
        turn_after(e,1,34);message(4,11,0xbf,cc[e],73);
        turn_after(e,-1,1);message(5,11,0xbf,cc[e],72); /* Reversals stay fine. */
        turn_after(e,-1,34);message(6,11,0xbf,cc[e],69);
        turn_after(e,-1,500);message(7,11,0xbf,cc[e],68);
        turn_after(e,-2,100);message(8,11,0xbf,cc[e],64); /* Two medium detents. */
        turn_after(e,-2,60);message(9,11,0xbf,cc[e],58);
        turn_after(e,INT_MIN,1);message(10,11,0xbf,cc[e],0);
        turn_after(e,INT_MAX,1);message(11,11,0xbf,cc[e],127);
        assert(n_out==12);
    }
    /* Each knob tracks its own direction/time; zero polls don't refresh it. */
    gateway_midi_values[EN_K1]=gateway_midi_values[EN_K2]=64;
    gateway_midi_turn_dir[EN_K1]=gateway_midi_turn_dir[EN_K2]=0;n_out=0;
    turn_after(EN_K1,1,0);turn_after(EN_K2,1,10);turn_after(EN_K1,1,10);
    message(0,11,0xbf,22,65);message(1,11,0xbf,23,65);message(2,11,0xbf,22,68);
    turn_after(EN_K1,0,100);turn_after(EN_K1,1,0);message(3,11,0xbf,22,69);
    gateway_midi_turn_ms[EN_K1]=UINT32_MAX-10u;now_ms=9;
    turn_after(EN_K1,1,0);message(4,11,0xbf,22,72);
    tap(B_REC);tap(B_REC);turn_after(EN_K1,1,0);message(5,11,0xbf,22,73);
    usb_links++;gateway_midi_service();turn_after(EN_K1,1,0);message(6,11,0xbf,22,74);
    tap(B_REC);assert(!gateway_midi_active);
    puts("PASS: gentle 1/2/3 MIDI acceleration on all seven encoders, exact thresholds, fine first/reversed/idle turns, batching, clamps/extreme deltas, independent timing, clock wrap, REC/link reset");
}
static void octave_lights(unsigned bright)
{
    uint32_t both=(1u<<B_OCTDN)|(1u<<B_OCTUP);
    uint32_t full=bright?1u<<bright:0;
    assert((lights_b&both)==full && (lights_db&both)==(both&~full));
}
static void octaves(void)
{
    app_input_only();tap(B_REC);assert(gateway_midi_active && !gateway_midi_octave);
    octave_lights(0);
    /* Transpose all keys through the whole supported range, without wrapping. */
    for(unsigned i=0;i<3;i++)tap(B_OCTDN);
    for(int octave=-3;octave<=3;octave++){
        assert(gateway_midi_octave==octave);
        octave_lights(octave<0?B_OCTDN:octave>0?B_OCTUP:0);
        if(octave==-3){tap(B_OCTDN);assert(gateway_midi_octave==-3);}
        if(octave==3){tap(B_OCTUP);assert(gateway_midi_octave==3);}
        for(unsigned k=0;k<27;k++){
            unsigned pitch=(unsigned)(53+(int)k+12*octave);
            n_out=0;key(k,1);message(0,9,0x9f,pitch,127);
            assert(lit_keys==(GATEWAY_MIDI_CS|(1u<<k)));
            key(k,0);message(1,8,0x8f,pitch,0);assert(n_out==2);
        }
        if(octave<3)tap(B_OCTUP);
    }
    /* Slow +/-2 and faster +/-3 blinking is clock driven, with a dim off phase. */
    tap(B_EDIT);app_loop();
    for(int sign=-1;sign<=1;sign+=2)for(int amount=1;amount<=3;amount++){
        int target=sign*amount;
        while(gateway_midi_octave!=target)tap(gateway_midi_octave<target?B_OCTUP:B_OCTDN);
        uint32_t anchor=gateway_midi_octave_ms;
        unsigned direction=sign<0?B_OCTDN:B_OCTUP;
        if(amount==1){now_ms+=1500;app_loop();octave_lights(direction);}
        else{
            unsigned half=amount==2?500u:200u;
            now_ms=anchor+half-1;app_loop();octave_lights(direction);
            now_ms=anchor+half;app_loop();octave_lights(0);
            now_ms=anchor+2*half;app_loop();octave_lights(direction);
            /* No display invalidation is needed just to update the LEDs. */
            gateway_page_redraw=0;now_ms=anchor+3*half;app_loop();
            octave_lights(0);assert(!gateway_page_redraw);
        }
    }
    gateway_midi_octave_ms=UINT32_MAX-100u;
    assert(gateway_midi_octave_light(98u) && !gateway_midi_octave_light(99u));
    gateway_midi_octave_ms=now_ms;gateway_lights();
    while(gateway_midi_octave)tap(B_OCTDN);
    held_btn=(1u<<B_OCTDN)|(1u<<B_OCTUP);app_input_only();assert(!gateway_midi_octave);
    held_btn=0;app_input_only();
    /* Reset any nonzero shift with simultaneous or staggered OCT presses. */
    for(int shift=-3;shift<=3;shift++)if(shift){
        while(gateway_midi_octave!=shift)tap(gateway_midi_octave<shift?B_OCTUP:B_OCTDN);
        held_btn=(1u<<B_OCTDN)|(1u<<B_OCTUP);app_input_only();
        assert(!gateway_midi_octave);octave_lights(0);held_btn=0;app_input_only();
    }
    for(unsigned first=0;first<2;first++){
        tap(B_OCTUP);tap(B_OCTUP);
        held_btn=1u<<(first?B_OCTUP:B_OCTDN);app_input_only();
        held_btn=(1u<<B_OCTDN)|(1u<<B_OCTUP);app_input_only();
        assert(!gateway_midi_octave);octave_lights(0);
        held_btn=1u<<(first?B_OCTDN:B_OCTUP);app_input_only();assert(!gateway_midi_octave);
        held_btn=0;app_input_only();
    }
    /* A long press transposes once and cannot start Bluetooth pairing. */
    held_btn=1u<<B_OCTUP;app_input_only();now_ms+=1500;app_input_only();app_loop();
    assert(gateway_midi_octave==1 && !gateway_oct_tracking && !plat_bt_receiver_pairing());
    octave_lights(B_OCTUP);held_btn=0;app_input_only();tap(B_OCTDN);
    /* Old-pitch note-offs precede the held chord at its new pitch. */
    key(0,1);key(26,1);n_out=0;tap(B_OCTUP);assert(n_out==4);
    message(0,8,0x8f,53,0);message(1,8,0x8f,79,0);
    message(2,9,0x9f,65,127);message(3,9,0x9f,91,127);
    midi_try_limited=1;midi_try_slots=0;n_out=0;
    tap(B_OCTUP);tap(B_OCTUP);key(26,0);
    assert(!n_out && gateway_midi_octave==3 && gateway_midi_sent_octave==1);
    midi_try_slots=1;gateway_midi_service();assert(n_out==1);
    message(0,8,0x8f,65,0);
    midi_try_slots=64;drain();assert(n_out==3);
    message(1,8,0x8f,91,0);message(2,9,0x9f,89,127);
    n_out=0;held_btn=(1u<<B_OCTDN)|(1u<<B_OCTUP);app_input_only();
    message(0,8,0x8f,89,0);message(1,9,0x9f,53,127);assert(n_out==2 && !gateway_midi_octave);
    held_btn=0;app_input_only();n_out=0;key(0,0);message(0,8,0x8f,53,0);midi_try_limited=0;
    for(unsigned i=0;i<3;i++)tap(B_OCTUP);
    /* Dialog OCT buttons retain cancel/confirm and full LEDs, without transpose. */
    gateway_settings_row=GATEWAY_SET_CODEC;tap(B_SEL);assert(gateway_codec_dialog);
    uint32_t both=(1u<<B_OCTDN)|(1u<<B_OCTUP);
    assert((lights_b&both)==both && !(lights_db&both));
    now_ms+=200;app_loop();assert((lights_b&both)==both);
    tap(B_OCTUP);assert(!gateway_codec_dialog && gateway_midi_octave==3);
    tap(B_SEL);tap(B_OCTDN);assert(!gateway_codec_dialog && gateway_midi_octave==3);
    tap(B_REC);assert(!gateway_midi_active && gateway_midi_octave==3);
    tap(B_REC);assert(gateway_midi_active && gateway_midi_octave==3);
    key(7,1);n_out=0;tap(B_REC);message(0,8,0x8f,96,0);key(7,0);
    app_init();app_ui_init();assert(!gateway_midi_octave && !gateway_midi_active);
    assert(!cv_cuts && !cpu_work_depth);
    puts("PASS: all 27 keys through +/-3 octaves, clamps, C lighting, directional bright/slow/fast LEDs and clock wrap, simultaneous/staggered OCT reset including held-note pitch, no held-button repeat or Bluetooth pairing, old-pitch releases under rapid transpose/backpressure, modal OCT priority, REC retention and boot reset");
}
int main(void){palette_set(APP_PALETTE);settings();n_out=0;controls();pressure();acceleration();octaves();}
