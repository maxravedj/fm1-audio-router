/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();app_loop();}
static void turn(unsigned e,int n){now_ms+=200u*(n<0?-n:n);enc_acc[e]=n;app_input_only();app_loop();}
static void reboot_app(void){held_btn=held_keys=0;app_init();app_ui_init();app_loop();}
static void positions(const uint8_t *want,int octave)
{
    assert(!memcmp(gateway_midi_values,want,NE) && !memcmp(gateway_midi_resume,want,NE));
    assert(gateway_midi_octave==octave && gateway_midi_resume_octave==octave);
}
static void record(const uint8_t *want,int octave)
{
    gateway_settings_t s;assert(plat_store_load(OBJ_SETTINGS,&s,sizeof s)==32 && s.version==GATEWAY_SETTINGS_VERSION && s.vectorscope_color<112u && (s.vectorscope_color&7u)<GATEWAY_WAVE_COLORS);
    assert(!memcmp(s.midi_position,want,NE));
    assert((int)(s.vectorscope_color>>4)-3==octave);
}
static void normal_exits(void)
{
    reboot_app();uint8_t initial[NE];memset(initial,64,NE);positions(initial,0);
    tap(B_EDIT);gateway_settings_row=GATEWAY_SET_MIDI;turn(EN_ALGO,1);tap(B_SAVE);tap(B_HOME);
    assert(gateway_midi_mode);record(initial,0);
    unsigned writes=store_writes;int fx_size=store_len[OBJ_FX];
    tap(B_REC);assert(gateway_midi_active);uint8_t saved[NE];
    for(unsigned e=0;e<NE;e++){turn(e,e+1);saved[e]=65+e;}
    tap(B_OCTUP);tap(B_OCTUP);
    now_ms+=1000;app_loop();assert(store_writes==writes && !memcmp(gateway_midi_resume,initial,NE));
    assert(gateway_midi_octave==2 && !gateway_midi_resume_octave);
    /* Main Settings SAVE while still active must not commit live controller state. */
    tap(B_EDIT);tap(B_SAVE);assert(gateway_midi_active);record(initial,0);writes=store_writes;
    n_out=0;tap(B_REC);assert(!gateway_midi_active && store_writes==writes+1 && !n_out);
    positions(saved,2);record(saved,2);assert(!gateway_midi_save_pending && !gateway_settings_dirty && store_len[OBJ_FX]==fx_size);
    /* Entry restores the exit bank, without sending an unsolicited CC burst. */
    memset(gateway_midi_values,0,NE);gateway_midi_octave=0;n_out=0;tap(B_REC);positions(saved,2);assert(!n_out);
    turn(EN_K1,1);assert(gateway_midi_values[EN_K1]==saved[EN_K1]+1);
    tap(B_OCTDN);
    writes=store_writes;reboot_app();positions(saved,2);assert(!gateway_midi_active && store_writes==writes);
    tap(B_REC);tap(B_REC);assert(store_writes==writes); /* Unchanged exit avoids flash wear. */
    puts("PASS: positions and octave saved immediately on REC exit, restored on entry/boot, no writes from turns or octave presses, active Settings SAVE keeps last exit, unchanged exit skips write, no entry CC burst or FX write");
}
static void failures(void)
{
    uint8_t saved[NE];memcpy(saved,gateway_midi_resume,NE);
    tap(B_REC);turn(EN_K2,3);saved[EN_K2]+=3;
    for(unsigned i=0;i<3;i++)tap(B_OCTDN);
    store_fail=1;unsigned writes=store_writes,tries=store_tries;tap(B_REC);
    assert(store_writes==writes && store_tries==tries+1 && gateway_midi_save_pending && gateway_settings_dirty);
    uint32_t failed_ms=gateway_settings_retry_ms;
    tap(B_REC);positions(saved,-1);turn(EN_K2,1);tap(B_OCTUP); /* New live edits while retry is pending. */
    now_ms=failed_ms+4999;app_loop();assert(store_tries==tries+1);
    store_fail=0;now_ms++;app_loop();record(saved,-1);
    assert(store_writes==writes+1 && !gateway_midi_save_pending && gateway_midi_values[EN_K2]==saved[EN_K2]+1 && !gateway_midi_octave);
    reboot_app();positions(saved,-1);
    /* A failed exit followed by power loss retains the last committed bank. */
    tap(B_REC);turn(EN_K4,5);tap(B_OCTUP);store_fail=1;tap(B_REC);store_fail=0;
    reboot_app();positions(saved,-1);record(saved,-1);
    /* A Settings change that leaves MIDI saves the current bank atomically. */
    tap(B_REC);turn(EN_K3,2);saved[EN_K3]+=2;tap(B_OCTDN);tap(B_OCTDN);tap(B_EDIT);gateway_midi_draft=0;
    store_fail=1;tap(B_SAVE);assert(gateway_midi_active && gateway_midi_mode && gateway_midi_resume_octave==-1);
    store_fail=0;tap(B_SAVE);assert(!gateway_midi_active && !gateway_midi_mode);record(saved,-3);
    reboot_app();positions(saved,-3);assert(!gateway_midi_mode);
    puts("PASS: failed exit preserves RAM and previous flash, five-second retry writes the exit positions/octave rather than later live edits, failed disable stays active and successful disable commits atomically");
}
static void migration(void)
{
    uint8_t initial[NE];memset(initial,64,NE);
    for(unsigned v=15;v<=18;v++){
        gateway_settings_t s=gateway_settings();s.version=v;s.zoom=9;s.reserved=1;
        s.bt_reserved[0]=v==15?2:v==16?3:19;s.bt_reserved[2]=GATEWAY_WAVE_ORANGE<<1;
        if(v==18){s.reserved|=96;s.bt_reserved[2]|=10u<<4;}
        memset(s.midi_position,255,NE);s.vectorscope_color=255;
        assert(!plat_store_save(OBJ_SETTINGS,&s,24));unsigned writes=store_writes;reboot_app();
        assert(gateway_zoom==9 && gateway_wave_color==GATEWAY_WAVE_ORANGE && store_writes==writes);
        positions(initial,0);assert(gateway_midi_mode==(v==18) && gateway_midi_channel==(v==18?10:0));
    }
    tap(B_REC);turn(EN_K1,1);tap(B_OCTDN);tap(B_REC);initial[EN_K1]++;record(initial,-1);reboot_app();positions(initial,-1);
    for(unsigned v=19;v<=21;v++){
        gateway_settings_t s=gateway_settings();s.version=v;s.zoom=9;
        s.vectorscope_color=v==19?0:v==20?GATEWAY_WAVE_ORANGE:GATEWAY_WAVE_RED|8u;
        assert(!plat_store_save(OBJ_SETTINGS,&s,32));unsigned writes=store_writes;reboot_app();
        assert(gateway_zoom==9 && store_writes==writes);positions(initial,0);
        assert(gateway_vectorscope_color==(v==19?GATEWAY_WAVE_GREEN:v==20?GATEWAY_WAVE_ORANGE:GATEWAY_WAVE_RED));
        /* Legacy reserved octave bits must still be rejected, not migrated. */
        s.vectorscope_color|=16u;assert(!plat_store_save(OBJ_SETTINGS,&s,32));reboot_app();assert(gateway_zoom==6);
        /* Restore the valid legacy record for the next iteration. */
        s.vectorscope_color&=15u;assert(!plat_store_save(OBJ_SETTINGS,&s,32));reboot_app();
    }
    for(unsigned bad=0;bad<NE+6;bad++){
        gateway_settings_t s=gateway_settings();s.zoom=9;
        if(bad<NE)s.midi_position[bad]=128;
        if(bad==NE)s.vectorscope_color=GATEWAY_WAVE_COLORS;
        if(bad==NE+3)s.version=18;
        if(bad==NE+4){s.version=22;s.vectorscope_color|=128u;}
        if(bad==NE+5)s.vectorscope_color=(s.vectorscope_color&15u)|112u;
        unsigned len=bad==NE+1?24:bad==NE+2?31:32;
        assert(!plat_store_save(OBJ_SETTINGS,&s,len));reboot_app();assert(gateway_zoom==6);
        uint8_t defaults[NE];memset(defaults,64,NE);positions(defaults,0);
    }
    for(unsigned rail=0;rail<=127;rail+=127){
        gateway_settings_t s=gateway_settings();s.zoom=9;memset(s.midi_position,rail,NE);
        assert(!plat_store_save(OBJ_SETTINGS,&s,32));reboot_app();assert(gateway_zoom==9);positions(s.midi_position,0);
    }
    assert(!cv_cuts && !cpu_work_depth);
    puts("PASS: v15-v21 migrate without boot writes, legacy octave defaults to zero and reserved bits stay validated, MIDI/channel/colors retained, first exit writes current/32 bytes, malformed positions/octave/reserved/length rejected, position rails restore exactly");
}
static void octave_exits(void)
{
    gateway_settings_t s=gateway_settings();s.reserved|=32u;
    s.vectorscope_color=GATEWAY_WAVE_ORANGE|8u|48u;
    assert(!plat_store_save(OBJ_SETTINGS,&s,32));reboot_app();
    for(int octave=-3;octave<=3;octave++){
        unsigned writes=store_writes;tap(B_REC);
        while(gateway_midi_octave!=octave)tap(gateway_midi_octave<octave?B_OCTUP:B_OCTDN);
        now_ms+=1000;app_loop();assert(store_writes==writes);
        tap(B_REC);record(s.midi_position,octave);assert(store_writes==writes+1);
        writes=store_writes;reboot_app();positions(s.midi_position,octave);
        assert(!gateway_midi_active && store_writes==writes && gateway_vectorscope_color==GATEWAY_WAVE_ORANGE && gateway_vectorscope_auto_gain);
        tap(B_REC);n_out=0;held_keys=1;app_input_only();
        assert(n_out==1 && out_pkt[0]==gateway_midi_packet(9,0x90u|gateway_midi_channel,53+12*octave,127));
        held_keys=0;app_input_only();tap(B_REC);assert(store_writes==writes);
    }
    /* A channel-changing Settings SAVE is also an exit; retain old-pitch releases. */
    tap(B_REC);tap(B_OCTDN);held_keys=1;app_input_only();n_out=0;
    unsigned old_channel=gateway_midi_channel;tap(B_EDIT);gateway_midi_channel_draft=(old_channel+1u)&15u;
    midi_try_limited=1;midi_try_slots=0;store_fail=1;tap(B_SAVE);
    assert(gateway_midi_active && gateway_midi_channel==old_channel && gateway_midi_resume_octave==3);
    store_fail=0;tap(B_SAVE);record(s.midi_position,2);
    assert(!gateway_midi_active && gateway_midi_sent==1 && !n_out);
    midi_try_slots=64;gateway_midi_service();
    assert(!gateway_midi_sent && n_out==1 && out_pkt[0]==gateway_midi_packet(8,0x80u|old_channel,77,0));
    midi_try_limited=0;held_keys=0;reboot_app();positions(s.midi_position,2);
    assert(gateway_midi_channel==((old_channel+1u)&15u) && !cv_cuts && !cpu_work_depth);
    puts("PASS: octave-only exits persist all seven shifts, entry sends notes at restored pitch, color/auto-scale bits survive, unchanged exits avoid writes, channel-change commits octave atomically and releases old-pitch/channel notes under USB backpressure");
}
int main(void){palette_set(APP_PALETTE);normal_exits();failures();migration();octave_exits();}
