/* SPDX-License-Identifier: MPL-2.0 */
/* Independently verify the device's embedded check against pinned, unmodified
 * upstream ldacBT_decode, rather than trusting the port's own result. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "ldacBT.h"
#include "../bt/br/ldac_dec/selftest_data.h"
int main(void)
{
    for(unsigned rate=0;rate<2;rate++){
        HANDLE_LDAC_BT d=ldacBT_get_handle();assert(d);
        assert(!ldacBT_init_handle_decode(d,LDACBT_CHANNEL_MODE_STEREO,rate?48000:44100,0,0,0));
        const struct ldac_probe_vector *v=ldac_probe_vectors+rate;
        uint32_t hash[2]={2166136261u,2166136261u};
        for(unsigned f=0;f<2;f++){
            uint8_t pcm[128*6];int used=0,got=0;
            assert(!ldacBT_decode(d,(unsigned char *)v->frames[f],pcm,LDACBT_SMPL_FMT_S24,sizeof v->frames[f],&used,&got));
            assert(used==sizeof v->frames[f] && got==sizeof pcm);
            if(!f)continue;
            for(unsigned i=0;i<128;i++)for(unsigned ch=0;ch<2;ch++){
                const uint8_t *p=pcm+(2*i+ch)*3;
                uint32_t sample=p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16;
                sample=(sample^0x800000u)-0x800000u;
                hash[ch]=(hash[ch]^sample)*16777619u;
            }
        }
        if(hash[0]!=v->hash[0] || hash[1]!=v->hash[1])
            fprintf(stderr,"probe %u Hz: actual %08x %08x expected %08x %08x\n",rate?48000:44100,hash[0],hash[1],v->hash[0],v->hash[1]);
        assert(hash[0]==v->hash[0] && hash[1]==v->hash[1]);
        ldacBT_free_handle(d);
    }
    puts("PASS: embedded CPU1 LDAC check vectors/left+right hashes match unmodified upstream decoding at both rates");
}
