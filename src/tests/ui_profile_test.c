/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define APP_UI_PROFILE
#include "../include/plat.h"
#include "../include/ui_profile.h"
static uint32_t ticks,xruns,c2ticks,ie=1,hardware,pending;
/* Match the actual flash helpers, deliberately NOT nesting-safe. The profiler
 * must use its own state-preserving guard rather than these main-thread APIs. */
static uint32_t irq_save(void){ie=0;return 0;}
static void irq_restore(uint32_t f){(void)f;ie=1;}
static uint32_t fm1_icfg(void){return ie?0x200:0;}
static void fm1_irq_off(void){ie=0;}
static void fm1_irq_on(void){ie=1;}
static uint32_t fm1_ticks(void){return ticks;}
#define FM1_TICKS_PER_US 24u
uint32_t plat_ticks24(void){return ticks;}
uint32_t plat_xruns(void){return xruns;}
uint32_t plat_profile_cpu2_ticks(void){return c2ticks;}
void plat_cpu2_sync(void){}
static void fm1_lcd_wait(void){ticks+=pending;pending=0;}
static void fm1_lcd_deselect(void){}
static void fm1_lcd_send_cmd(uint8_t c){(void)c;hardware++;pending=24;}
static void fm1_lcd_send_data(const void *p,uint32_t n){(void)p;hardware++;pending=n*24;}
static void fm1_lcd_hw_init(void){}
static void fm1_lcd_baud(uint32_t b){(void)b;}
#include "../device/ui_profile.c"
#include "../device/lcd.c"
#include "../device/plat_lock.c"
#include "../device/cpu_workload.h"
static void reset(void){plat_profile_stop();cpu_work_depth=cpu_work_ticks=0;}
int main(void)
{
    assert(!plat_profile_flags() && ui_profile_idle_spins==64);
    assert(!plat_profile_start(0,0) && !plat_profile_start(31,0) && !plat_profile_start(1,16));
    assert(!plat_profile_start(1,PROFILE_NO_WAVE_CAPTURE));
    assert(plat_profile_start(1,PROFILE_NO_LCD|PROFILE_IDLE_SLOW));
    assert(!plat_profile_start(1,0) && ui_profile_idle_spins==4096);
    /* IRQ probes and probes under an outer platform lock must never enable
     * interrupts. The earlier fake irq_restore restored IE and hid this bug. */
    fm1_irq_off();
    plat_profile_add(PROFILE_AUDIO,24);assert(!ie);
    plat_profile_count(PROFILE_BYTES,1);assert(!ie);
    plat_profile_span_t isr=plat_profile_begin();ticks+=24;
    plat_profile_end(PROFILE_AUDIO,isr);assert(!ie);
    fm1_irq_on();
    uint32_t outer=plat_lock();
    plat_profile_add(PROFILE_WORK,24);assert(!ie);
    plat_profile_count(PROFILE_BYTES,1);assert(!ie);
    plat_unlock(outer);assert(ie);
    /* Restart so later exact counter assertions have a clean baseline. */
    plat_profile_stop();assert(plat_profile_start(1,PROFILE_NO_LCD|PROFILE_IDLE_SLOW));
    uint16_t pixels[16]={0};unsigned sent=hardware;
    lcd_blit(0,0,4,4,pixels);lcd_fill(4,4,4,4,0);
    assert(hardware==sent && !lcd_busy && plat_profile_counter(PROFILE_WINDOWS)==2);
    assert(plat_profile_counter(PROFILE_BYTES)==2*(11+32));
    assert(plat_profile_counter(PROFILE_DMA)==6);
    plat_profile_span_t span=plat_profile_begin();ticks+=240;
    plat_profile_end(PROFILE_WAVE_READ,span);assert(plat_profile_row(PROFILE_WAVE_READ).ticks==240);
    uint32_t f=plat_lock();ticks+=120;uint32_t nested=plat_lock();ticks+=120;plat_unlock(nested);plat_unlock(f);
    assert(ie && plat_profile_row(PROFILE_LOCK).ticks==240 && plat_profile_row(PROFILE_LOCK).calls==1);
    f=irq_save();nested=plat_lock();ticks+=24;plat_unlock(nested);assert(!ie);irq_restore(f);
    c2ticks=24000;xruns=3;ticks+=24000000;
    assert(plat_profile_poll() && !plat_profile_flags() && ui_profile_idle_spins==64);
    assert(plat_profile_info(3)==3 && plat_profile_info(4)==24000);
    plat_profile_row_t frozen=plat_profile_row(PROFILE_WAVE_READ);
    plat_profile_end(PROFILE_WAVE_READ,span);assert(plat_profile_row(PROFILE_WAVE_READ).ticks==frozen.ticks);
    assert(plat_profile_start(1,0));
    plat_profile_end(PROFILE_WAVE_READ,span);assert(!plat_profile_row(PROFILE_WAVE_READ).calls);
    sent=hardware;lcd_blit(0,0,4,4,pixels);assert(hardware==sent+6 && lcd_busy);
    plat_profile_stop();assert(!lcd_busy && plat_profile_row(PROFILE_LCD_WAIT).calls==6);
    assert(plat_profile_row(PROFILE_LCD_WAIT).ticks==(11+32)*24);
    /* TIMER4 wrap and a partially open workload window at stop. */
    ticks=UINT32_MAX-2000;cpu_work_depth=0;plat_cpu_work_reset();
    assert(plat_profile_start(1,0));span=plat_profile_begin();
    plat_cpu_work_begin();ticks+=240;plat_cpu_work_begin();ticks+=240;plat_cpu_work_end();
    ticks+=240;plat_cpu_work_end();assert(plat_profile_row(PROFILE_WORK).ticks==720);
    plat_cpu_work_begin();ticks+=240;plat_profile_stop();
    assert(plat_profile_row(PROFILE_WORK).ticks==960);plat_cpu_work_end();
    assert(plat_profile_info(1)==960 && ie);
    /* The panel's partial live snapshot must not double-count WORK time. */
    ticks=0;plat_cpu_work_reset();assert(plat_profile_start(1,0));plat_cpu_work_begin();
    ticks=250*24000;assert(plat_cpu_work_pct()==100);
    ticks=300*24000;plat_cpu_work_end();plat_profile_stop();
    assert(plat_profile_row(PROFILE_WORK).ticks==300*24000);
    reset();puts("PASS: actual profiler/LCD/IRQ-lock/workload: IRQ-enable preservation with real flash-helper semantics, timeout, flags, DMA suppression/drain, nested work, epoch, timer wrap and partial snapshots");
}
