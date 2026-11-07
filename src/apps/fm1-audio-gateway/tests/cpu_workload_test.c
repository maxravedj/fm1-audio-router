/* SPDX-License-Identifier: MPL-2.0
 * Deterministic timer: nested timer/audio/UI work must never be counted twice. */
#include <assert.h>
#include <stdio.h>
#include "plat.h"
static uint32_t ticks;
uint32_t plat_ticks24(void) { return ticks; }
uint32_t plat_lock(void) { return 0; }
void plat_unlock(uint32_t f) { (void)f; }
#include "cpu_workload.h"
void plat_cpu2_sync(void) {}
#include "cpu2_workload.h"
#define MS(n) ((uint32_t)(n)*24000u)
int main(void)
{
    ticks=0;plat_cpu_work_reset();
    ticks=MS(10);plat_cpu_work_begin();
    ticks=MS(20);plat_cpu_work_begin();
    ticks=MS(40);plat_cpu_work_end();
    ticks=MS(60);plat_cpu_work_end(); /* 50 ms, including nested IRQ */
    ticks=MS(80);plat_cpu_work_begin();ticks=MS(130);plat_cpu_work_end(); /* another 50 */
    ticks=MS(250);assert(plat_cpu_work_pct()==40);
    ticks=MS(500);assert(plat_cpu_work_pct()==0);
    ticks=MS(600);plat_cpu_work_begin();
    ticks=MS(700);plat_cpu_work_begin();
    ticks=MS(750);assert(plat_cpu_work_pct()==60); /* snapshot inside active nested work */
    ticks=MS(800);plat_cpu_work_end();ticks=MS(850);plat_cpu_work_end();
    ticks=MS(1000);assert(plat_cpu_work_pct()==40);
    ticks=MS(1250);assert(plat_cpu_work_pct()==0);
    ticks=4294000000u;plat_cpu_work_reset();
    ticks+=MS(5);plat_cpu_work_begin();ticks+=MS(50);plat_cpu_work_end();
    ticks+=MS(195);assert(plat_cpu_work_pct()==20); /* 24-MHz timer wrap */
    assert(!cpu_work_depth);
    ticks=0;
    ticks=MS(10);cpu2_work_begin();
    ticks=MS(60);cpu2_work_end();
    ticks=MS(250);assert(plat_cpu2_work_pct()==20);
    ticks=MS(500);assert(plat_cpu2_work_pct()==0);
    ticks=MS(600);cpu2_work_begin();
    ticks=MS(750);assert(plat_cpu2_work_pct()==60);
    ticks=MS(800);cpu2_work_end();
    ticks=MS(1000);assert(plat_cpu2_work_pct()==20);
    ticks=MS(1250);assert(plat_cpu2_work_pct()==0);
    cpu2_work_window=ticks=4294000000u;
    ticks+=MS(5);cpu2_work_begin();ticks+=MS(50);cpu2_work_end();
    ticks+=MS(195);assert(plat_cpu2_work_pct()==20);
    cpu2_work.seq++;ticks+=MS(250);
    assert(plat_cpu2_work_pct()==20); /* torn/in-progress sample never blocks CPU0 */
    puts("PASS: CPU meter measures wall-time workload, nested IRQ/UI overlap once, live snapshots, idle, timer wrap");
    puts("PASS: independent CPU2 job load, active snapshots, idle decay, timer wrap and bounded seqlock read");
}
