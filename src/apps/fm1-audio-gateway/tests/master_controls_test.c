/* SPDX-License-Identifier: MPL-2.0
 * Production BSP mix: dB knob taper, meter tap, then user mute ramp. */
#include "plat.h"
uint32_t plat_lock(void) { return 0; }
void plat_unlock(uint32_t lock) { (void)lock; }
#define APP_MASTER_METER
#define APP_MASTER_FADE
#define APP_MASTER_DB
#include "fm1b_config.h"
#define main original_mix_test_main
#include "../../../tests/uac_mix_test.c"
#undef main
#include <assert.h>
#include <stdlib.h>
static int32_t pcm[2*N];
static void render(uint32_t master, int full)
{
    memset(pcm,0,sizeof(pcm));feed(N);
    uac_mix(pcm,N,APP_GAIN(master),master,full,4096);
    ua_r=ua_w;
}
int main(void)
{
    unsigned i,m,full;
    uint32_t peaks[2];
    usb.config=1;uac.feed=1;uo.alt=1;feed(UO_TGT);
    for(m=1;m<=4096;m++)assert(APP_GAIN(m)>=APP_GAIN(m-1));
    assert(APP_GAIN(0)==0 && APP_GAIN(1024)==23 && APP_GAIN(2048)==130 &&
           APP_GAIN(3072)==728 && APP_GAIN(4096)==4096);
    for(full=0;full<2;full++) for(m=0;m<=4096;m+=1024) {
        const uint32_t gains[]={0,23,130,728,4096};
        int32_t expected=PC*256*(int32_t)gains[m/1024]/4096;
        plat_master_peaks(peaks);render(m,full);plat_master_peaks(peaks);
        assert(near(pcm[0],expected));
        assert(near(pcm[1],-expected));
        assert(near(peaks[0],expected));
    }
    plat_master_mute(1);
    for(m=0;m<200;m++) {
        plat_master_peaks(peaks);render(4096,0);plat_master_peaks(peaks);
        assert(near(peaks[0],PC*256) && near(peaks[1],PC*256));
        if(m==0)assert(pcm[0]>0 && pcm[0]<PC*256);
    }
    assert(master_fade_gain==0 && pcm[0]==0 && pcm[1]==0);
    assert(uo.go && !uo.underruns); /* muted output still drains USB normally */
    plat_master_mute(0);
    for(m=0;m<200;m++){render(4096,0);if(m==0)assert(pcm[0]>0 && pcm[0]<PC*256);}
    assert(master_fade_gain==65536 && near(pcm[0],PC*256));
    /* Ramp shape and reversal, with one-frame calls across render boundaries. */
    plat_master_mute(1);
    {int32_t previous=1000000;
     for(i=0;i<4410;i++) {
        int32_t lr[2]={1000000,-500000};master_fade_apply(lr,1);
        assert(lr[0]<=previous && previous-lr[0]<=230);previous=lr[0];
        assert(lr[1]<=0 && lr[1]>=-500000);
        if(i==4408)assert(master_fade_gain>0);
     }
     assert(previous==0);
    }
    plat_master_mute(0);
    for(i=0;i<200;i++){int32_t lr[2]={1000000,-500000};master_fade_apply(lr,1);}
    {uint32_t before=master_fade_gain;int32_t lr[2]={1000000,-500000};
     plat_master_mute(1);master_fade_apply(lr,1);
     assert(before-master_fade_gain>=14 && before-master_fade_gain<=15);
     plat_master_mute(0);master_fade_apply(lr,1);assert(abs((int)master_fade_gain-(int)before)<=1);
    }
    plat_master_mute_init(0);plat_master_meter_enable(0);
    render(4096,0);plat_master_peaks(peaks);
    assert(!peaks[0] && !peaks[1] && near(pcm[0],PC*256));
    plat_master_meter_enable(1);render(4096,0);plat_master_peaks(peaks);
    assert(near(peaks[0],PC*256) && near(pcm[0],PC*256));
    puts("PASS: real mix has -60..0 dB MASTER, 100-ms smooth/reversible mute, unchanged pre-fade meters, USB drains while muted");
}
