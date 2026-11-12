/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();app_loop();}
static void turn(unsigned e,int n){now_ms+=200u*(n<0?-n:n);enc_acc[e]=n;app_input_only();app_loop();}
static void reboot_app(void){held_btn=held_keys=0;app_init();app_ui_init();app_loop();}
static void positions(const uint8_t *want)
{
    assert(!memcmp(gateway_midi_values,want,NE) && !memcmp(gateway_midi_resume,want,NE));
}
static void record(const uint8_t *want)
{
    gateway_settings_t s;assert(plat_store_load(OBJ_SETTINGS,&s,sizeof s)==32 && s.version==19 && !s.midi_reserved);
    assert(!memcmp(s.midi_position,want,NE));
}
static void normal_exits(void)
{
    reboot_app();uint8_t initial[NE];memset(initial,64,NE);positions(initial);
    tap(B_EDIT);gateway_settings_row=GATEWAY_SET_MIDI;turn(EN_ALGO,1);tap(B_SAVE);tap(B_HOME);
    assert(gateway_midi_mode);record(initial);
    unsigned writes=store_writes;int fx_size=store_len[OBJ_FX];
    tap(B_REC);assert(gateway_midi_active);uint8_t saved[NE];
    for(unsigned e=0;e<NE;e++){turn(e,e+1);saved[e]=65+e;}
    now_ms+=1000;app_loop();assert(store_writes==writes && !memcmp(gateway_midi_resume,initial,NE));
    /* Main Settings SAVE while still active must not commit live positions. */
    tap(B_EDIT);tap(B_SAVE);assert(gateway_midi_active);record(initial);writes=store_writes;
    n_out=0;tap(B_REC);assert(!gateway_midi_active && store_writes==writes+1 && !n_out);
    positions(saved);record(saved);assert(!gateway_midi_save_pending && !gateway_settings_dirty && store_len[OBJ_FX]==fx_size);
    /* Entry restores the exit bank, without sending an unsolicited CC burst. */
    memset(gateway_midi_values,0,NE);n_out=0;tap(B_REC);positions(saved);assert(!n_out);
    turn(EN_K1,1);assert(gateway_midi_values[EN_K1]==saved[EN_K1]+1);
    writes=store_writes;reboot_app();positions(saved);assert(!gateway_midi_active && store_writes==writes);
    tap(B_REC);tap(B_REC);assert(store_writes==writes); /* Unchanged exit avoids flash wear. */
    puts("PASS: seven positions saved immediately on REC exit, restored on entry/boot, no writes from turns, active Settings SAVE keeps last exit, unchanged exit skips write, no entry CC burst or FX write");
}
static void failures(void)
{
    uint8_t saved[NE];memcpy(saved,gateway_midi_resume,NE);
    tap(B_REC);turn(EN_K2,3);saved[EN_K2]+=3;
    store_fail=1;unsigned writes=store_writes,tries=store_tries;tap(B_REC);
    assert(store_writes==writes && store_tries==tries+1 && gateway_midi_save_pending && gateway_settings_dirty);
    uint32_t failed_ms=gateway_settings_retry_ms;
    tap(B_REC);positions(saved);turn(EN_K2,1); /* New unsaved edit while retry is pending. */
    now_ms=failed_ms+4999;app_loop();assert(store_tries==tries+1);
    store_fail=0;now_ms++;app_loop();record(saved);
    assert(store_writes==writes+1 && !gateway_midi_save_pending && gateway_midi_values[EN_K2]==saved[EN_K2]+1);
    reboot_app();positions(saved);
    /* A failed exit followed by power loss retains the last committed bank. */
    tap(B_REC);turn(EN_K4,5);store_fail=1;tap(B_REC);store_fail=0;
    reboot_app();positions(saved);record(saved);
    /* A Settings change that leaves MIDI saves the current bank atomically. */
    tap(B_REC);turn(EN_K3,2);saved[EN_K3]+=2;tap(B_EDIT);gateway_midi_draft=0;
    store_fail=1;tap(B_SAVE);assert(gateway_midi_active && gateway_midi_mode);
    store_fail=0;tap(B_SAVE);assert(!gateway_midi_active && !gateway_midi_mode);record(saved);
    reboot_app();positions(saved);assert(!gateway_midi_mode);
    puts("PASS: failed exit preserves RAM and previous flash, five-second retry writes the exit snapshot rather than later live turns, failed disable stays active and successful disable commits bank atomically");
}
static void migration(void)
{
    uint8_t initial[NE];memset(initial,64,NE);
    for(unsigned v=15;v<=18;v++){
        gateway_settings_t s=gateway_settings();s.version=v;s.zoom=9;s.reserved=1;
        s.bt_reserved[0]=v==15?2:v==16?3:19;s.bt_reserved[2]=GATEWAY_WAVE_ORANGE<<1;
        if(v==18){s.reserved|=96;s.bt_reserved[2]|=10u<<4;}
        memset(s.midi_position,255,NE);s.midi_reserved=255;
        assert(!plat_store_save(OBJ_SETTINGS,&s,24));unsigned writes=store_writes;reboot_app();
        assert(gateway_zoom==9 && gateway_wave_color==GATEWAY_WAVE_ORANGE && store_writes==writes);
        positions(initial);assert(gateway_midi_mode==(v==18) && gateway_midi_channel==(v==18?10:0));
    }
    tap(B_REC);turn(EN_K1,1);tap(B_REC);initial[EN_K1]++;record(initial);reboot_app();positions(initial);
    for(unsigned bad=0;bad<NE+4;bad++){
        gateway_settings_t s=gateway_settings();s.zoom=9;
        if(bad<NE)s.midi_position[bad]=128;
        if(bad==NE)s.midi_reserved=1;
        if(bad==NE+3)s.version=18;
        unsigned len=bad==NE+1?24:bad==NE+2?31:32;
        assert(!plat_store_save(OBJ_SETTINGS,&s,len));reboot_app();assert(gateway_zoom==6);
        uint8_t defaults[NE];memset(defaults,64,NE);positions(defaults);
    }
    for(unsigned rail=0;rail<=127;rail+=127){
        gateway_settings_t s=gateway_settings();s.zoom=9;memset(s.midi_position,rail,NE);
        assert(!plat_store_save(OBJ_SETTINGS,&s,32));reboot_app();assert(gateway_zoom==9);positions(s.midi_position);
    }
    assert(!cv_cuts && !cpu_work_depth);
    puts("PASS: v15-v18 24-byte settings migrate without a boot write and default MIDI positions to 64, v18 MIDI/channel retained, first exit writes v19/32 bytes, malformed positions/reserved/length rejected, 0 and 127 restore exactly");
}
int main(void){palette_set(APP_PALETTE);normal_exits();failures();migration();}
