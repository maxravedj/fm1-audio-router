/* SPDX-License-Identifier: MPL-2.0 */
/* Actual decoder on a host thread while CPU0 assembles the next element. */
#define main transport_test_main
#include "bt_aac_transport.c"
#undef main
#include <pthread.h>
static void element(FILE *f,const uint8_t conf[6])
{
    uint8_t packet[1200];uint32_t n;
    do {n=u32(f);assert(n && n<=sizeof packet && fread(packet,1,n,f)==n);deliver(packet,n,conf);}while(!(packet[1]&128));
}
static void *thread_job(void *unused)
{
    (void)unused;test_core=1;worker_jobs++;worker_fn(worker_arg);test_core=0;return 0;
}
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    uint8_t conf[6],saved[BTR_AAC_MUX];assert(fread(conf,1,6,f)==6);
    memory_limit=100000;hold_worker=1;
    element(f,conf);assert(worker_fn && btr_aac->active && !pcm_frames);
    uint32_t bytes=btr_aac->job_bytes;
    memcpy(saved,btr_aac->job_mux,bytes);
    pthread_t thread;assert(!pthread_create(&thread,0,thread_job,0));
    element(f,conf);assert(btr_aac->pending && btr_aac->active);
    assert(!memcmp(saved,btr_aac->job_mux,bytes));
    /* Packet bursts queue independently of both the assembler and worker. */
    struct btr_aac_packet queued=btr_aac->packets[1];
    uint8_t queued_copy[BTR_AAC_MUX];memcpy(queued_copy,btr_aac->queue+queued.start,queued.bytes);
    element(f,conf);assert(btr_aac->packet_w-btr_aac->packet_r==3);
    assert(!memcmp(queued_copy,btr_aac->queue+queued.start,queued.bytes));
    assert(!pthread_join(thread,0));worker_fn=0;
    btr_aac_poll();assert(worker_fn);
    while(worker_fn){complete_worker();btr_aac_poll();}
    assert(pcm_frames==3072 && worker_jobs==3);
    /* Lifecycle teardown waits for an in-flight job and drops its queued
     * successor, rather than freeing memory the worker still reads/writes. */
    element(f,conf);assert(worker_fn);element(f,conf);assert(btr_aac->pending);
    unsigned jobs=worker_jobs;br_reset();
    assert(!btr_aac && !worker_fn && !allocations && worker_jobs==jobs+1);
    /* Disconnect/reconfigure can immediately reuse all scratch for a fresh
     * stream; no stale queued result may publish after that reset. */
    rewind(f);assert(fread(conf,1,6,f)==6);element(f,conf);assert(worker_fn);
    unsigned frames=pcm_frames;br_reset();assert(pcm_frames==frames+1024);
    assert(!allocations && !worker_fn && !btr_aac);
    /* Fill beyond both queue limits while the worker is held. The executing
     * payload must remain intact; overload drops whole elements and recovers. */
    uint8_t full[BTR_AAC_MUX+12]={0x80,0xe0,0,1,0,0,0,0,0,0,0,17};
    memcpy(full+12,saved,bytes);unsigned gaps=btr_stats.gaps;
    for(unsigned i=0;i<24;i++){
        full[3]=(uint8_t)i;full[6]=(uint8_t)(i*4);btr_aac_media(full,bytes+12,conf);
        assert(btr_aac->packet_w-btr_aac->packet_r<=BTR_AAC_QUEUE_SLOTS);
        assert(btr_aac->queue_w-btr_aac->queue_r<=BTR_AAC_QUEUE_BYTES);
        assert(!memcmp(saved,btr_aac->job_mux,bytes));
    }
    assert(btr_stats.gaps>gaps);btr_aac_reset();assert(!allocations && !worker_fn);
    full[3]=0;full[6]=0;btr_aac_media(full,bytes+12,conf);complete_worker();btr_aac_poll();
    btr_aac->queue_w=btr_aac->queue_r=UINT32_MAX-127u;
    btr_aac->packet_w=btr_aac->packet_r=UINT32_MAX-1u;
    for(unsigned i=1;i<=4;i++){
        full[3]=(uint8_t)i;full[6]=(uint8_t)(i*4);btr_aac_media(full,bytes+12,conf);
        assert(!memcmp(saved,btr_aac->job_mux,bytes));complete_worker();btr_aac_poll();
    }
    assert(btr_aac->queue_w==btr_aac->queue_r && btr_aac->packet_w==btr_aac->packet_r);
    btr_aac_reset();assert(!allocations);
    fclose(f);
    puts("PASS: real CPU2-thread AAC, concurrent assembly, bounded queue/backpressure, byte/packet wrap, join-before-free and fresh reset");
}
