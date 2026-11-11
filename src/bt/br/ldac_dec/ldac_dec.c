/* SPDX-License-Identifier: Apache-2.0 */
#include "ldac_dec.h"
#define _DECODE_ONLY
#define _32BIT_FIXED_POINT
#define FM1_LDAC_FREESTANDING
#define FM1_LDAC_1FS_ONLY
/* Consume the wide product before returning to memory-heavy decode loops.
 * Returning raw int64 leaves callers free to load/spill before using it. */
static __attribute__((noinline)) int32_t ldac_fm1_mul_rsft(int32_t a,int32_t b,int shift)
{
    return (int32_t)(((int64_t)a*b+((int64_t)1<<(shift-1)))>>shift);
}
static __attribute__((noinline)) int32_t ldac_fm1_mul_lsft(int32_t a,int32_t b,int shift)
{
    return (int32_t)((int64_t)a*b<<(-shift));
}
#include "upstream/src/ldac.h"
static int ldac_fm1_abs(int value)
{
    return value<0 ? (int)(0u-(unsigned)value) : value;
}
/* The upstream bump allocator is used only during initialization, with an
 * exactly sized arena. Disable every libc heap fallback in this translation
 * unit. No public upstream heap API is used by this adapter. */
#define malloc(n) ((void)(n),(void *)0)
#define calloc(n,s) ((void)(n),(void)(s),(void *)0)
#define free(p) ((void)(p))
#define abs(n) ldac_fm1_abs(n)
#include "upstream/src/ldaclib.c"
#undef malloc
#undef calloc
#undef free
#undef abs

#define LDAC_ALIGN(n) (((n)+7u)&~7u)
#define LDAC_ARENA (2u*LDAC_ALIGN(sizeof(AC))+2u*LDAC_ALIGN(sizeof(ACSUB))+LDAC_ALIGN(sizeof(AB)))
#define LDAC_MAGIC 0x4c444143u
struct fm1_ldac_dec {
    uint32_t magic,rate;
    HANDLE_LDAC_STRUCT core;
    uint8_t arena[LDAC_ARENA] __attribute__((aligned(8)));
    /* Upstream's reader peeks three bytes, guarded at byte 1024. Copy and
     * pad the payload so neither short frames nor malformed streams can
     * read caller memory past their input. Final consumed length is checked. */
    uint8_t payload[LDAC_MAXNBYTES+2];
    uint8_t planar[2][FM1_LDAC_SAMPLES*3];
};
typedef char ldac_int_is_32[(sizeof(int)==4 && sizeof(INT32)==4)?1:-1];

size_t fm1_ldac_dec_size(void) { return sizeof(fm1_ldac_dec); }
size_t fm1_ldac_frame_size(const uint8_t *p,size_t bytes,uint32_t rate)
{
    if(!p || bytes<3 || (rate!=44100 && rate!=48000) || p[0]!=LDAC_SYNCWORD ||
       (p[1]>>5)!=(rate==48000) || ((p[1]>>3)&3)!=LDAC_CHCONFIGID_ST)return 0;
    unsigned n=(((unsigned)p[1]&7u)<<6 | p[2]>>2)+1;
    return n>=2*LDAC_MINSUPNBYTES && n<=LDAC_MAXSUPNBYTES && n+3u<=bytes?n+3u:0;
}
static int overlaps(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y ? y-x<an : x-y<bn;
}
fm1_ldac_dec *fm1_ldac_dec_init(void *memory,size_t bytes,uint32_t rate)
{
    if(!memory || ((uintptr_t)memory&(FM1_LDAC_ALIGNMENT-1u)) || bytes<sizeof(fm1_ldac_dec) ||
       (rate!=44100 && rate!=48000))return NULL;
    fm1_ldac_dec *d=memory;
    memset(d,0,sizeof *d);d->rate=rate;
    d->core.sfinfo.p_mempos=(char *)d->arena;
    if(ldaclib_set_config_info(&d->core,rate==48000,LDAC_CHCONFIGID_ST,327,0)!=LDAC_S_OK ||
       ldaclib_init_decode(&d->core,0)!=LDAC_S_OK ||
       d->core.sfinfo.p_mempos!=(char *)d->arena+sizeof d->arena){
        memset(d,0,sizeof *d);return NULL;
    }
    d->magic=LDAC_MAGIC;return d;
}
int fm1_ldac_dec_reset(fm1_ldac_dec *d)
{
    if(!d || d->magic!=LDAC_MAGIC)return FM1_LDAC_ARGUMENT;
    return fm1_ldac_dec_init(d,sizeof *d,d->rate)?0:FM1_LDAC_DATA;
}
void fm1_ldac_dec_destroy(fm1_ldac_dec *d)
{
    if(d && d->magic==LDAC_MAGIC)memset(d,0,sizeof *d);
}
int fm1_ldac_dec_frame(fm1_ldac_dec *d,const uint8_t *frame,size_t bytes,
                       int32_t (*pcm)[2],size_t capacity)
{
    if(!d || d->magic!=LDAC_MAGIC || !frame || !pcm || ((uintptr_t)pcm&3u) ||
       capacity<FM1_LDAC_SAMPLES ||
       overlaps(d,sizeof *d,frame,bytes) ||
       overlaps(d,sizeof *d,pcm,FM1_LDAC_SAMPLES*sizeof *pcm) ||
       overlaps(frame,bytes,pcm,FM1_LDAC_SAMPLES*sizeof *pcm))return FM1_LDAC_ARGUMENT;
    if(bytes<3 || fm1_ldac_frame_size(frame,bytes,d->rate)!=bytes)goto frame_error;
    unsigned rate=frame[1]>>5,channels=(frame[1]>>3)&3;
    unsigned payload=(((unsigned)frame[1]&7u)<<6 | frame[2]>>2)+1;
    if(rate!=(d->rate==48000) || channels!=LDAC_CHCONFIGID_ST || bytes!=payload+3u)
        goto frame_error;
    if(ldaclib_set_config_info(&d->core,rate,channels,payload,frame[2]&3)!=LDAC_S_OK)
        goto frame_error;
    memset(d->payload,0,sizeof d->payload);memcpy(d->payload,frame+3,payload);
    char *planes[2]={(char *)d->planar[0],(char *)d->planar[1]};int used=0;
    /* Reject soft errors as well as fatal ones; never publish suspect PCM. */
    if(ldaclib_decode(&d->core,d->payload,planes,payload,&used,LDAC_SMPL_FMT_S24)!=LDAC_S_OK || used!=(int)payload){
        fm1_ldac_dec_reset(d);return FM1_LDAC_DATA;
    }
    for(unsigned i=0;i<FM1_LDAC_SAMPLES;i++)for(unsigned ch=0;ch<2;ch++){
        const uint8_t *p=d->planar[ch]+3*i;
        uint32_t v=p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16;
        pcm[i][ch]=(int32_t)(v^0x800000u)-0x800000;
    }
    return FM1_LDAC_SAMPLES;
frame_error:
    fm1_ldac_dec_reset(d);return FM1_LDAC_FRAME;
}

#include "selftest_data.h"
unsigned fm1_ldac_dec_check(fm1_ldac_dec *d,int32_t (*pcm)[2],uint32_t hash[2])
{
    if(!d || d->magic!=LDAC_MAGIC || !pcm || !hash)return 0;
    const struct ldac_probe_vector *v=&ldac_probe_vectors[d->rate==48000];
    hash[0]=hash[1]=2166136261u;
    unsigned ok=0;
    if(fm1_ldac_dec_reset(d))return 0;
    for(unsigned f=0;f<2;f++){
        if(fm1_ldac_dec_frame(d,v->frames[f],sizeof v->frames[f],pcm,FM1_LDAC_SAMPLES)!=FM1_LDAC_SAMPLES)goto done;
        if(!f)continue; /* First frame primes the transform overlap. */
        for(unsigned i=0;i<FM1_LDAC_SAMPLES;i++)for(unsigned ch=0;ch<2;ch++)
            hash[ch]=(hash[ch]^(uint32_t)pcm[i][ch])*16777619u;
    }
    ok=(hash[0]==v->hash[0])|((hash[1]==v->hash[1])<<1);
done:
    /* Probe tones never reach the PCM ring; live history starts empty. */
    if(fm1_ldac_dec_reset(d))ok=0;
    return ok;
}
