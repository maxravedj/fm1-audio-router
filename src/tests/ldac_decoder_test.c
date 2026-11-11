/* SPDX-License-Identifier: Apache-2.0 */
#include "../bt/br/ldac_dec/ldac_dec.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t rng=731;
static unsigned random_word(void) { rng=rng*1664525u+1013904223u;return rng; }
int main(int argc,char **argv)
{
    assert(argc==3);uint32_t rate=atoi(argv[1]);size_t size=fm1_ldac_dec_size();
    unsigned char *mem=malloc(size),*other=malloc(size);assert(mem && other);
    int32_t out[128][2],baseline[128][2];uint8_t frame[FM1_LDAC_MAX_FRAME];
    assert(!fm1_ldac_dec_init(NULL,size,rate));assert(!fm1_ldac_dec_init(mem+1,size,rate));
    assert(!fm1_ldac_dec_init(mem,size-1,rate));assert(!fm1_ldac_dec_init(mem,size,96000));
    fm1_ldac_dec *d=fm1_ldac_dec_init(mem,size,rate);assert(d);
    uint32_t hash[2];assert(fm1_ldac_dec_check(d,out,hash)==3);
    /* Probe teardown must leave exactly the initial live overlap state. */
    FILE *in=fopen(argv[2],"rb");assert(in);unsigned records=0,nonzero=0;
    while(1){
        int lo=fgetc(in);if(lo==EOF)break;int hi=fgetc(in);assert(hi>=0);unsigned n=lo+(hi<<8);
        assert(n<=sizeof frame && fread(frame,1,n,in)==n);uint8_t expected[128*6];
        assert(fread(expected,1,sizeof expected,in)==sizeof expected);
        assert(fm1_ldac_dec_frame(d,frame,n,out,128)==128);
        for(unsigned i=0;i<128;i++)for(unsigned ch=0;ch<2;ch++){
            const uint8_t *p=expected+(i*2+ch)*3;
            int32_t v=(int32_t)((p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16)^0x800000u)-0x800000;
            assert(out[i][ch]==v);nonzero+=v!=0;
        }
        records++;
    }
    fclose(in);assert(records>100 && nonzero>1000);
    unsigned n=(((frame[1]&7u)<<6)|(frame[2]>>2))+4;
    assert(!fm1_ldac_dec_reset(d));assert(fm1_ldac_dec_frame(d,frame,n,baseline,128)==128);
    for(unsigned cut=0;cut<n;cut++){
        uint8_t *short_frame=malloc(cut?cut:1);assert(short_frame);
        memcpy(short_frame,frame,cut);
        assert(fm1_ldac_dec_frame(d,short_frame,cut,out,128)<0);
        free(short_frame);
    }
    assert(fm1_ldac_dec_frame(d,frame,n,out,128)==128 && !memcmp(out,baseline,sizeof out));
    assert(!fm1_ldac_dec_reset(d));memset(out,0x5a,sizeof out);
    assert(fm1_ldac_dec_frame(d,frame,n,out,127)==FM1_LDAC_ARGUMENT);
    for(unsigned i=0;i<sizeof out;i++)assert(((uint8_t *)out)[i]==0x5a);
    assert(fm1_ldac_dec_frame(d,mem,32,out,128)==FM1_LDAC_ARGUMENT);
    assert(fm1_ldac_dec_frame(d,frame,n,(int32_t (*)[2])mem,128)==FM1_LDAC_ARGUMENT);
    assert(fm1_ldac_dec_frame(d,(uint8_t *)out,32,out,128)==FM1_LDAC_ARGUMENT);
    assert(fm1_ldac_dec_frame(NULL,frame,n,out,128)==FM1_LDAC_ARGUMENT);
    assert(fm1_ldac_dec_frame(d,NULL,n,out,128)==FM1_LDAC_ARGUMENT);
    assert(fm1_ldac_dec_frame(d,frame,n,NULL,128)==FM1_LDAC_ARGUMENT);
    assert(fm1_ldac_dec_frame(d,frame,n,(int32_t (*)[2])((uint8_t *)out+1),128)==FM1_LDAC_ARGUMENT);
    assert(fm1_ldac_dec_frame(d,frame,n,out,128)==128 && !memcmp(out,baseline,sizeof out));
    uint8_t bad[FM1_LDAC_MAX_FRAME+1];
    for(unsigned kind=0;kind<7;kind++){
        memcpy(bad,frame,n);unsigned bytes=n;
        if(kind==0)bad[0]^=1;                         /* sync */
        if(kind==1)bad[1]^=0x20;                     /* wrong negotiated rate */
        if(kind==2)bad[1]=(bad[1]&0x1f)|0x40;        /* 88.2 kHz */
        if(kind==3)bad[1]=(bad[1]&0xe7)|0x08;        /* dual channel */
        if(kind==4)bad[2]^=4;                        /* frame size mismatch */
        if(kind==5)bytes=FM1_LDAC_MAX_FRAME+1;        /* oversized */
        if(kind==6){bad[bytes++]=0;}                 /* trailing bytes */
        assert(fm1_ldac_dec_frame(d,bad,bytes,out,128)==FM1_LDAC_FRAME);
        assert(fm1_ldac_dec_frame(d,frame,n,out,128)==128 && !memcmp(out,baseline,sizeof out));
    }
    uint8_t fuzz[FM1_LDAC_MAX_FRAME];
    for(unsigned trial=0;trial<20000;trial++){
        unsigned len=4+random_word()%512;
        for(unsigned j=0;j<len;j++)fuzz[j]=random_word()>>24;
        if(trial&1){memcpy(fuzz,frame,n);len=n;fuzz[3+random_word()%(n-3)]^=1u<<(random_word()%8);}
        fuzz[0]=0xaa;unsigned payload=len-4;
        fuzz[1]=((rate==48000)<<5)|(2<<3)|(payload>>6);fuzz[2]=(payload<<2)|(random_word()&3);
        int result=fm1_ldac_dec_frame(d,fuzz,len,out,128);assert(result==128 || result<0);
    }
    /* Independent contexts/reset must not inherit another stream's history. */
    fm1_ldac_dec *second=fm1_ldac_dec_init(other,size,rate);assert(second);
    assert(!fm1_ldac_dec_reset(d));
    assert(fm1_ldac_dec_frame(d,frame,n,out,128)==128);
    assert(fm1_ldac_dec_frame(second,frame,n,baseline,128)==128 && !memcmp(out,baseline,sizeof out));
    fm1_ldac_dec_destroy(d);for(unsigned i=0;i<size;i++)assert(!mem[i]);
    assert(fm1_ldac_dec_frame(d,frame,n,out,128)==FM1_LDAC_ARGUMENT);
    fm1_ldac_dec_destroy(second);free(other);free(mem);
    printf("PASS: %u bit-exact stereo frames, 24-bit PCM, truncation/reset/alias/capacity and 20000 malformed/mutated frames; workspace %zu B\n",records,size);
}
