/* SPDX-License-Identifier: MPL-2.0 */
/* CPU2 owns these samples; CPU0 snapshots them without stopping either core. */
static struct {
    volatile uint32_t seq,ticks,start,active;
} cpu2_work;
static uint32_t cpu2_work_window,cpu2_work_previous,cpu2_work_percent;
#ifdef APP_UI_PROFILE
uint32_t plat_profile_cpu2_ticks(void)
{
    static uint32_t previous;
    for(unsigned tries=0;tries<8;tries++){
        uint32_t seq=cpu2_work.seq;plat_cpu2_sync();
        uint32_t total=cpu2_work.ticks,active=cpu2_work.active,start=cpu2_work.start,now=plat_ticks24();
        plat_cpu2_sync();
        if(!(seq&1u) && seq==cpu2_work.seq){previous=total+(active?now-start:0);break;}
    }
    return previous;
}
#endif
static void cpu2_work_begin(void)
{
    cpu2_work.seq++;plat_cpu2_sync();
    cpu2_work.start=plat_ticks24();cpu2_work.active=1;
    plat_cpu2_sync();cpu2_work.seq++;
}
static void cpu2_work_end(void)
{
    uint32_t now=plat_ticks24();
    cpu2_work.seq++;plat_cpu2_sync();
    cpu2_work.ticks+=now-cpu2_work.start;cpu2_work.active=0;
    plat_cpu2_sync();cpu2_work.seq++;
}
uint32_t plat_cpu2_work_pct(void)
{
    uint32_t seq,total,now,active,start;
    for(unsigned tries=0;;tries++) {
        if(tries==8)return cpu2_work_percent; /* never trap CPU0 on an interrupted/faulted writer */
        seq=cpu2_work.seq;plat_cpu2_sync();
        total=cpu2_work.ticks;active=cpu2_work.active;start=cpu2_work.start;
        now=plat_ticks24();plat_cpu2_sync();
        if(!(seq&1u) && seq==cpu2_work.seq)break;
    }
    if(active)total+=now-start;
    uint32_t dt=now-cpu2_work_window;
    if(dt>=250u*1000u*PLAT_TICKS_PER_US){
        uint32_t busy=total-cpu2_work_previous;
        cpu2_work_percent=busy>=dt?100u:busy/(dt/100u);
        cpu2_work_previous=total;cpu2_work_window=now;
    }
    return cpu2_work_percent;
}
