/* SPDX-License-Identifier: Apache-2.0 */
/* Trusted encoder fixtures and unmodified upstream fixed-point decoder oracle. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ldacBT.h"
int main(int argc,char **argv)
{
    assert(argc==4 || argc==5);int rate=atoi(argv[1]),quality=atoi(argv[2]);
    int quiet=argc==5 && !strcmp(argv[4],"quiet");
    FILE *out=fopen(argv[3],"wb");assert(out);
    HANDLE_LDAC_BT enc=ldacBT_get_handle(),dec=ldacBT_get_handle();assert(enc && dec);
    assert(!ldacBT_init_handle_encode(enc,1000,quality==3?0:quality,LDACBT_CHANNEL_MODE_STEREO,
        quiet?LDACBT_SMPL_FMT_S24:LDACBT_SMPL_FMT_S16,rate));
    assert(!ldacBT_init_handle_decode(dec,LDACBT_CHANNEL_MODE_STEREO,rate,0,0,0));
    unsigned records=0;uint32_t rng=731;
    for(unsigned block=0;block<160;block++){
        /* ABR changes frame lengths without resetting overlap history. */
        if(quality==3 && block && !(block%24))assert(!ldacBT_set_eqmid(enc,(block/24)%3));
        union {int16_t s16[128][2];unsigned char s24[128*6];} pcm;
        unsigned char packet[2048];int consumed=0,written=0,frames=0;
        for(unsigned i=0;i<128;i++)for(unsigned ch=0;ch<2;ch++){
            unsigned t=block*128+i;rng=rng*1664525u+1013904223u;
            if(quiet){
                /* -40..-130 dBFS in 10-dB steps, retaining the low eight
                 * bits. Independent stereo tones exercise real low-level
                 * transform output, not only the readout's dB conversion. */
                double db=-40.0-10.0*(block/16);
                uint32_t v=(uint32_t)(int32_t)(8388607*pow(10,db/20)*sin(6.283185307179586*(ch?2341:997)*t/rate));
                unsigned at=(2*i+ch)*3;pcm.s24[at]=v;pcm.s24[at+1]=v>>8;pcm.s24[at+2]=v>>16;
            }else pcm.s16[i][ch]=block<8?0:block<80?(int16_t)(24000*sin(6.283185307179586*(ch?2341:997)*t/rate)):
                block<120?(int16_t)((t+ch)&1?32767:-32768):(int16_t)(rng>>16);
        }
        assert(!ldacBT_encode(enc,&pcm,&consumed,packet,&written,&frames));
        assert(consumed==(quiet?sizeof pcm.s24:sizeof pcm.s16));
        unsigned at=0;
        for(int f=0;f<frames;f++){
            assert(packet[at]==0xaa);
            unsigned n=((((unsigned)packet[at+1]&7)<<6)|(packet[at+2]>>2))+4;
            assert(at+n<=(unsigned)written);
            unsigned char decoded[128*6];int used=0,got=0;
            assert(!ldacBT_decode(dec,packet+at,decoded,LDACBT_SMPL_FMT_S24,n,&used,&got));
            assert(used==(int)n && got==sizeof decoded);
            fputc(n&255,out);fputc(n>>8,out);
            assert(fwrite(packet+at,1,n,out)==n);
            assert(fwrite(decoded,1,sizeof decoded,out)==sizeof decoded);
            at+=n;records++;
        }
        assert(at==(unsigned)written);
    }
    assert(records>100);fclose(out);ldacBT_free_handle(enc);ldacBT_free_handle(dec);
    printf("reference: %d Hz quality %d, %u real LDAC frames\n",rate,quality,records);
}
