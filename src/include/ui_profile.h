/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <stdint.h>
/* Development-only, CPU0 measurements. Stage times include preemption and
 * overlap; WORK is the existing nesting-aware workload union, not their sum. */
enum { PROFILE_WORK, PROFILE_AUDIO, PROFILE_USB, PROFILE_MASTER, PROFILE_OUT,
       PROFILE_BT_MIX, PROFILE_WAVE_READ, PROFILE_WAVE_DRAW, PROFILE_SPEC_READ,
       PROFILE_FFT, PROFILE_SPEC_DRAW, PROFILE_LCD_WAIT, PROFILE_LOCK, PROFILE_ROWS };
enum { PROFILE_WINDOWS, PROFILE_BYTES, PROFILE_DMA, PROFILE_COUNTS };
enum { PROFILE_NO_LCD=1, PROFILE_NO_WAVE_UI=2, PROFILE_NO_WAVE_CAPTURE=4,
       PROFILE_IDLE_SLOW=8, PROFILE_FLAGS=15 };
typedef struct {uint32_t calls,ticks,max_ticks;} plat_profile_row_t;
typedef struct {uint32_t ticks,epoch;} plat_profile_span_t;
#ifdef APP_UI_PROFILE
extern volatile uint32_t ui_profile_flags,ui_profile_idle_spins;
static inline uint32_t plat_profile_flags(void){return ui_profile_flags;}
plat_profile_span_t plat_profile_begin(void);
void plat_profile_end(unsigned row,plat_profile_span_t span);
void plat_profile_add(unsigned row,uint32_t ticks);
void plat_profile_count(unsigned counter,uint32_t n);
int plat_profile_start(uint32_t seconds,uint32_t flags);
int plat_profile_stop(void);
int plat_profile_poll(void);
uint32_t plat_profile_info(unsigned field); /* 0 running, 1 duration ticks, 2 flags, 3 xruns, 4 CPU2 job ticks */
uint32_t plat_profile_cpu2_ticks(void);
uint32_t plat_profile_work_pending(void);
plat_profile_row_t plat_profile_row(unsigned row);
uint32_t plat_profile_counter(unsigned counter);
#else
static inline uint32_t plat_profile_flags(void){return 0;}
static inline plat_profile_span_t plat_profile_begin(void){return (plat_profile_span_t){0,0};}
static inline void plat_profile_end(unsigned row,plat_profile_span_t span){(void)row;(void)span;}
static inline void plat_profile_add(unsigned row,uint32_t ticks){(void)row;(void)ticks;}
static inline void plat_profile_count(unsigned counter,uint32_t n){(void)counter;(void)n;}
#endif
