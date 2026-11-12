/* SPDX-License-Identifier: MPL-2.0 */
/* Exercise real BSP string encoding, with the app's TOML product macro supplied
 * by the focused runner. No invented VID/PID or USB interface changes. */
#define main existing_uac_main
#include "../../../bsp/tests/uac_test.c"
#undef main
int main(void)
{
    const uint8_t *p;uint16_t n;
    static const char *const want[]={"FM1 Audio"};
    for(unsigned i=0;i<1;i++){
        assert(get_desc(0x0302+i,&p,&n));
        assert(n==2+2*strlen(want[i]) && p[0]==n && p[1]==3);
        for(unsigned j=0;want[i][j];j++)assert(p[2+2*j]==want[i][j] && !p[3+2*j]);
    }
    test_descriptors();
    assert(get_desc(0x0320,&p,&n) && n==10 && p[0]==10 && p[1]==3);
    const char *midi="MIDI";
    for(unsigned j=0;j<4;j++)assert(p[2+2*j]==midi[j] && !p[3+2*j]);
    assert(get_desc(0x0200,&p,&n));
    unsigned interfaces=0,jacks=0,sub=0;
    for(unsigned i=0;i<n;i+=p[i]){
        const uint8_t *d=p+i;
        if(d[1]==4){sub=d[6];if(d[5]==1 && sub==3){assert(d[8]==32);interfaces++;}}
        else if(d[1]==0x24 && sub==3 && (d[2]==2 || d[2]==3)){
            assert(d[d[0]-1]==32);jacks++;
        }
    }
    assert(interfaces==1 && jacks==4);
    /* Real bounded USB queue: rejection cannot silently consume a note-off. */
    usb.config=1;mo_w=mo_r=0;
    for(unsigned i=0;i<64;i++)assert(midi_out_try(0x017fb00bu+i));
    assert(!midi_out_try(0x00358008u) && mo_w==64);
    mo_r++;assert(midi_out_try(0x00358008u) && midi_out_q[0]==0x00358008u);
    usb.config=0;assert(!midi_out_try(0x00358008u));
    assert(!get_desc(0x0303,&p,&n)); /* app keeps the combined USB function */
    puts("PASS: actual BSP FM1 Audio product, MIDI UTF-16 port/interface/four-jack names, bounded/disconnected MIDI queue; combined UAC1/asynchronous feedback intact");
}
