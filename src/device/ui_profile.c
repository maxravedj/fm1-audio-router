/* SPDX-License-Identifier: MPL-2.0
 * Bounded CPU0-only diagnostics; no allocation, flash, formatting or joins in
 * measured paths. Preserve the actual interrupt-enable bit independently of
 * plat_lock: profiling that lock must not recurse. The flash driver's irq_save/
 * irq_restore always enable on restore and cannot protect nested/ISR probes. */
#include "ui_profile.h"
static void lcd_sync(void);
static inline uint32_t ui_profile_irq_save(void)
{
    uint32_t enabled=fm1_icfg()&(1u<<9);
    fm1_irq_off();return enabled;
}
static inline void ui_profile_irq_restore(uint32_t enabled)
{
    if(enabled)fm1_irq_on();
}
volatile uint32_t ui_profile_flags,ui_profile_idle_spins=64;
static volatile uint32_t ui_profile_epoch;
static uint32_t ui_profile_serial,ui_profile_start,ui_profile_duration,ui_profile_limit;
static uint32_t ui_profile_saved_flags,ui_profile_xruns,ui_profile_initial_xruns;
static uint32_t ui_profile_cpu2_start,ui_profile_cpu2_ticks;
static plat_profile_row_t ui_profile_rows[PROFILE_ROWS];
static uint32_t ui_profile_counts[PROFILE_COUNTS];
plat_profile_span_t plat_profile_begin(void)
{
    uint32_t epoch=ui_profile_epoch;
    return (plat_profile_span_t){epoch?plat_ticks24():0,epoch};
}
void plat_profile_add(unsigned row,uint32_t ticks)
{
    if(!ui_profile_epoch || row>=PROFILE_ROWS)return;
    uint32_t f=ui_profile_irq_save();
    if(ui_profile_epoch){
        plat_profile_row_t *r=&ui_profile_rows[row];
        r->calls++;r->ticks+=ticks;if(ticks>r->max_ticks)r->max_ticks=ticks;
    }
    ui_profile_irq_restore(f);
}
void plat_profile_end(unsigned row,plat_profile_span_t span)
{
    if(span.epoch && span.epoch==ui_profile_epoch)plat_profile_add(row,plat_ticks24()-span.ticks);
}
void plat_profile_count(unsigned counter,uint32_t n)
{
    if(!ui_profile_epoch || counter>=PROFILE_COUNTS)return;
    uint32_t f=ui_profile_irq_save();if(ui_profile_epoch)ui_profile_counts[counter]+=n;ui_profile_irq_restore(f);
}
int plat_profile_start(uint32_t seconds,uint32_t flags)
{
    if(ui_profile_epoch || !seconds || seconds>30 || flags>PROFILE_FLAGS ||
       ((flags&PROFILE_NO_WAVE_CAPTURE) && !(flags&PROFILE_NO_WAVE_UI)))return 0;
    lcd_sync(); /* complete the previous frame before resetting/timing */
    uint32_t f=ui_profile_irq_save();
    memset(ui_profile_rows,0,sizeof ui_profile_rows);memset(ui_profile_counts,0,sizeof ui_profile_counts);
    ui_profile_saved_flags=flags;ui_profile_flags=flags;ui_profile_idle_spins=flags&PROFILE_IDLE_SLOW?4096:64;
    plat_cpu2_sync();
    ui_profile_initial_xruns=plat_xruns();ui_profile_xruns=0;ui_profile_duration=0;
    ui_profile_cpu2_start=plat_profile_cpu2_ticks();ui_profile_cpu2_ticks=0;
    ui_profile_start=plat_ticks24();ui_profile_limit=seconds*1000000u*PLAT_TICKS_PER_US;
    if(!++ui_profile_serial)ui_profile_serial=1;
    ui_profile_epoch=ui_profile_serial;ui_profile_irq_restore(f);return 1;
}
int plat_profile_stop(void)
{
    if(!ui_profile_epoch)return 0;
    lcd_sync(); /* count the final outstanding DMA wait inside the window */
    uint32_t f=ui_profile_irq_save();
    ui_profile_duration=plat_ticks24()-ui_profile_start;ui_profile_xruns=plat_xruns()-ui_profile_initial_xruns;
    ui_profile_cpu2_ticks=plat_profile_cpu2_ticks()-ui_profile_cpu2_start;
    uint32_t partial=plat_profile_work_pending();
    if(partial)plat_profile_add(PROFILE_WORK,partial);
    ui_profile_epoch=0;ui_profile_flags=0;ui_profile_idle_spins=64;
    plat_cpu2_sync();
    ui_profile_irq_restore(f);return 1;
}
int plat_profile_poll(void)
{
    /* Main thread only: never change LCD suppression halfway through a blit. */
    return ui_profile_epoch && plat_ticks24()-ui_profile_start>=ui_profile_limit?plat_profile_stop():0;
}
uint32_t plat_profile_info(unsigned field)
{
    if(field==0)return ui_profile_epoch!=0;
    if(field==1)return ui_profile_epoch?plat_ticks24()-ui_profile_start:ui_profile_duration;
    if(field==2)return ui_profile_saved_flags;
    if(field==4)return ui_profile_epoch?plat_profile_cpu2_ticks()-ui_profile_cpu2_start:ui_profile_cpu2_ticks;
    return ui_profile_epoch?plat_xruns()-ui_profile_initial_xruns:ui_profile_xruns;
}
plat_profile_row_t plat_profile_row(unsigned row)
{
    /* Reporting is main-thread and only after stop, so there are no writers. */
    return row<PROFILE_ROWS?ui_profile_rows[row]:(plat_profile_row_t){0,0,0};
}
uint32_t plat_profile_counter(unsigned counter){return counter<PROFILE_COUNTS?ui_profile_counts[counter]:0;}
