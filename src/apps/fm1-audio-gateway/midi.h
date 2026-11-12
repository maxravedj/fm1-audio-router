/* SPDX-License-Identifier: MPL-2.0 */
/* CPU0 foreground only. Reuse the USB queue; retain releases under backpressure.
 * F3..G5 plus +/-3 octaves, velocity 127. Absolute CC: mild 1/2/3 acceleration.
 * Encoder positions resume from the last normal exit; fresh installs use 64. */
static uint8_t gateway_midi_mode,gateway_midi_draft;
static uint8_t gateway_midi_c,gateway_midi_c_draft;
static uint8_t gateway_midi_channel,gateway_midi_channel_draft;
static uint8_t gateway_midi_active,gateway_midi_releasing,gateway_midi_sent_channel;
static int8_t gateway_midi_octave,gateway_midi_sent_octave;
static uint8_t gateway_midi_octave_on;
static uint32_t gateway_midi_octave_ms;
static uint8_t gateway_midi_usb_error,gateway_midi_rec_on;
static uint32_t gateway_midi_usb_error_ms;
static uint32_t gateway_midi_wanted,gateway_midi_sent,gateway_midi_link;
static uint8_t gateway_midi_values[NE],gateway_midi_pending;
static uint8_t gateway_midi_resume[NE],gateway_midi_save_pending;
static uint32_t gateway_midi_turn_ms[NE];
static int8_t gateway_midi_turn_dir[NE];
#define GATEWAY_MIDI_KEYS ((1u<<27)-1u)
#define GATEWAY_MIDI_CS ((1u<<7)|(1u<<19)) /* C4, C5; key 0 is MIDI 53 (F3). */
static void gateway_midi_stop(void)
{
    gateway_midi_active=0;gateway_midi_wanted=0;gateway_midi_releasing=1;
    gateway_midi_pending=0;
    gateway_midi_usb_error=0;
    memset(gateway_midi_turn_dir,0,sizeof gateway_midi_turn_dir);
}
static void gateway_midi_exit(void)
{
    if(gateway_midi_active && memcmp(gateway_midi_resume,gateway_midi_values,NE)){
        memcpy(gateway_midi_resume,gateway_midi_values,NE);
        gateway_midi_save_pending=1;gateway_settings_changed();
    }
    if(gateway_midi_active && gateway_midi_save_pending)gateway_settings_retry_ms=plat_ms()-5000u;
    gateway_midi_stop();
}
static int gateway_midi_positions_valid(const uint8_t *values)
{
    for(unsigned e=0;e<NE;e++)if(values[e]>127)return 0;
    return 1;
}
static void gateway_midi_init(void)
{
    gateway_midi_mode=gateway_midi_draft=gateway_midi_c=gateway_midi_c_draft=0;
    gateway_midi_channel=gateway_midi_channel_draft=0;
    gateway_midi_octave=gateway_midi_sent_octave=0;gateway_midi_octave_on=0;
    gateway_midi_rec_on=0;
    gateway_midi_octave_ms=plat_ms();
    gateway_midi_stop();gateway_midi_sent=0;
    memset(gateway_midi_values,64,sizeof gateway_midi_values);
    memset(gateway_midi_resume,64,sizeof gateway_midi_resume);gateway_midi_save_pending=0;
    gateway_midi_link=plat_midi_out_link();gateway_midi_sent_channel=0;
}
static int gateway_midi_octave_light(uint32_t now)
{
    unsigned octave=gateway_midi_octave<0?-gateway_midi_octave:gateway_midi_octave;
    return octave==1 || (octave>1 && !((now-gateway_midi_octave_ms)/(octave==2?500u:200u)&1u));
}
static int gateway_midi_rec_light(uint32_t now)
{
    uint32_t elapsed=now-gateway_midi_usb_error_ms;
    return gateway_midi_active || (gateway_midi_usb_error && (elapsed<150u || (elapsed>=300u && elapsed<450u)));
}
static void gateway_midi_turn(unsigned encoder,int32_t delta)
{
    if(!delta)return;
    /* Clamp before addition: remote/accumulated INT_MIN/MAX cannot overflow. */
    if(delta>127)delta=127;if(delta< -127)delta=-127;
    int direction=delta>0?1:-1;
    uint32_t now=plat_ms(),elapsed=now-gateway_midi_turn_ms[encoder];
    unsigned count=(unsigned)(delta<0?-delta:delta),step=1;
    if(gateway_midi_turn_dir[encoder]==direction){
        /* Scale thresholds by batched detents, avoiding division in foreground. */
        if(elapsed<35u*count)step=3;
        else if(elapsed<80u*count)step=2;
    }
    gateway_midi_turn_ms[encoder]=now;gateway_midi_turn_dir[encoder]=(int8_t)direction;
    int32_t value=gateway_midi_values[encoder]+delta*(int32_t)step;
    if(value>127)value=127;if(value<0)value=0;
    if(value!=gateway_midi_values[encoder]){
        gateway_midi_values[encoder]=(uint8_t)value;
        gateway_midi_pending|=(uint8_t)(1u<<encoder);
    }
}
static uint32_t gateway_midi_packet(unsigned cin,unsigned status,unsigned d1,unsigned d2)
{
    return cin | status<<8 | d1<<16 | d2<<24;
}
static void gateway_midi_service(void)
{
    unsigned budget=8;
    uint32_t link=plat_midi_out_link();
    if(link!=gateway_midi_link){
        gateway_midi_link=link;gateway_midi_sent=0;gateway_midi_releasing=0;
        gateway_midi_pending=0;
        memset(gateway_midi_turn_dir,0,sizeof gateway_midi_turn_dir);
    }
    if(!gateway_midi_active && !gateway_midi_sent)return;
    if(gateway_midi_sent_channel!=gateway_midi_channel || gateway_midi_sent_octave!=gateway_midi_octave)gateway_midi_releasing=1;
    uint32_t off=gateway_midi_sent & (gateway_midi_releasing?GATEWAY_MIDI_KEYS:~gateway_midi_wanted);
    for(unsigned key=0;off && key<27 && budget;key++)if(off&(1u<<key)){
        unsigned note=(unsigned)(53+(int)key+12*gateway_midi_sent_octave);
        if(!plat_midi_try_out(gateway_midi_packet(8,0x80u|gateway_midi_sent_channel,note,0)))return;
        gateway_midi_sent&=~(1u<<key);budget--;
    }
    if(gateway_midi_releasing){
        if(gateway_midi_sent)return;
        gateway_midi_releasing=0;gateway_midi_sent_channel=gateway_midi_channel;gateway_midi_sent_octave=gateway_midi_octave;
    }
    if(!gateway_midi_active)return;
    uint32_t on=gateway_midi_wanted & ~gateway_midi_sent;
    for(unsigned key=0;on && key<27 && budget;key++)if(on&(1u<<key)){
        unsigned note=(unsigned)(53+(int)key+12*gateway_midi_octave);
        if(!plat_midi_try_out(gateway_midi_packet(9,0x90u|gateway_midi_channel,note,127)))return;
        gateway_midi_sent|=1u<<key;budget--;
    }
    for(unsigned e=0;e<NE && budget;e++)if(gateway_midi_pending&(1u<<e)){
        unsigned cc=e==EN_PRESET?26u:e>=EN_K1?22u+e-EN_K1:20u+e;
        if(!plat_midi_try_out(gateway_midi_packet(11,0xb0u|gateway_midi_channel,cc,gateway_midi_values[e])))return;
        gateway_midi_pending&=(uint8_t)~(1u<<e);budget--;
    }
}
