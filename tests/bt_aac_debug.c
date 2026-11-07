/* SPDX-License-Identifier: MPL-2.0 */
/* Exercise the actual AAC-enabled diagnostic formatter with full traces. */
#define main transport_test_main
#include "bt_aac_transport.c"
#undef main
static uint32_t fm1_ms,btr_rate;
static struct {
    uint32_t phase,started,last_good,good,bad,syncs,carries,bit,offset,rxbit,status;
    uint8_t encrypted,reconnect_hold;
} brl;
static struct {uint32_t st;} brl_ssp;
static struct {uint32_t w,r;} blt;
static uint32_t irq_save(void){return 0;}
static void irq_restore(uint32_t f){(void)f;}
static uint32_t btl_opcode(const uint8_t *p,uint32_t n,uint32_t *tid)
{(void)p;(void)n;*tid=0;return 0;}
static char *put_dec(char *p,uint32_t n)
{char b[10];unsigned k=0;do{b[k++]=(char)('0'+n%10);n/=10;}while(n);while(k)*p++=b[--k];*p=0;return p;}
#include "../bt/br/bt_receiver_debug.h"
int main(void)
{
    memset(&br_fault,255,sizeof br_fault);br_fault.trace_n=BR_TRACE_N;
    memset(&btr_stats,255,sizeof btr_stats);
    brc.profile.codec=BTR_CODEC_AAC;btr_rate=48000;
    btr_aac_max_us=btr_aac_memory=UINT32_MAX;
    assert(!strncmp(br_debug_line(14),"BT codec ",9));
    assert(!strncmp(br_debug_line(15),"BT audio ",9));
    assert(!strncmp(br_debug_line(19),"BT AAC cfg ",11));
    assert(!strncmp(br_debug_line(20),"BT trace ",9));
    unsigned rows=0;const char *line;
    while((line=br_debug_line(rows))){assert(strlen(line)<=43);rows++;}
    assert(rows==36);
    br_fault.trace_n=0;
    assert(br_debug_line(19) && !br_debug_line(20));
    puts("PASS: AAC media counters precede full fault traces, 36 rows, bounded 43-byte USB payloads");
}
