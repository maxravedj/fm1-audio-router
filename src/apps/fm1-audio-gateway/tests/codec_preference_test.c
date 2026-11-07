/* SPDX-License-Identifier: MPL-2.0 */
#define APP_BT_AAC
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../bt/br/bt_receiver_profile.c"
int main(void)
{
    uint8_t out[64],discover[]={0x10,1},caps[]={0x20,12,4};
    const uint8_t sbc[]={0x30,3,4,12,1,0,7,6,0,0,0x21,0x15,2,53};
    const uint8_t aac[]={0x40,3,8,16,1,0,7,8,0,2,0x40,1,4,0x84,0xe2,0};
    for(unsigned pref=0;pref<3;pref++){
        btr_codec_preference=pref;struct btr_profile s={0};
        unsigned n=btr_signal(&s,discover,sizeof discover,out);
        assert((out[0]&3)==2 && n==(pref?4:6));
        if(pref==1)assert(out[2]==8 && out[3]==8);
        else assert(out[2]==4 && out[3]==8);
        if(!pref)assert(out[4]==8 && out[5]==8);
        for(unsigned codec=0;codec<2;codec++){
            unsigned allowed=codec?pref!=2:pref!=1;
            caps[2]=codec?8:4;n=btr_signal(&s,caps,sizeof caps,out);
            assert((out[0]&3)==(allowed?2:3));
            if(allowed)assert(out[7]==(codec?2:0));
            memset(&s,0,sizeof s);
            n=btr_signal(&s,codec?aac:sbc,codec?sizeof aac:sizeof sbc,out);
            assert((out[0]&3)==(allowed?2:3));
            assert(s.state==(allowed?BTR_CONFIGURED:BTR_IDLE));
            if(allowed)assert(s.codec==(codec?BTR_CODEC_AAC:BTR_CODEC_SBC));
            memset(&s,0,sizeof s);
        }
    }
    puts("PASS: real AVDTP AUTO advertises both endpoints, AAC/SBC advertise only selected endpoint; capabilities and SET_CONFIGURATION reject disabled codecs");
}
