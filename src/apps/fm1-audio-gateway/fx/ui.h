/* SPDX-License-Identifier: MPL-2.0
 * Live master FX controls and independently versioned A/B settings. */
#include "master_fx.h"
typedef struct { uint8_t magic[3],version; fx_params_t params; } gateway_fx_record_t;
static fx_engine_t gateway_fx;
static fx_params_t gateway_fx_params;
static uint8_t gateway_fx_row,gateway_fx_dirty,gateway_fx_notice,gateway_fx_limiter;
static gateway_meter_t gateway_fx_meter[2];
static uint32_t gateway_fx_meter_ms,gateway_fx_reduction;
static uint32_t gateway_fx_ms,gateway_fx_retry_ms,gateway_fx_draw_ms,gateway_fx_fault_seen;
static const char *const gateway_fx_names[]={"Chain","EQ / post gain","Band 1","Band 2","Band 3","Band 4","Band 5","Peak limiter"};
static const char *const gateway_fx_types[]={"BELL","LOW PASS","HIGH PASS","LOW SHELF","HIGH SHELF"};
static void gateway_fx_publish(const fx_params_t *p);
#include "presets.h"
static void gateway_fx_init(void)
{
    fx_bank_t b;gateway_fx_presets_load();fx_build(&gateway_fx_params,&b);fx_init(&gateway_fx,&b);
    gateway_fx_fault_seen=0;gateway_fx_dirty=gateway_fx_notice=gateway_fx_limiter=0;gateway_fx_row=2;
    gateway_fx_dialog=gateway_fx_save_tracking=gateway_fx_save_held=0;
    gateway_fx_ms=plat_ms();gateway_fx_retry_ms=plat_ms()-5000u;
}
static void gateway_fx_publish(const fx_params_t *p)
{
    fx_bank_t b;
    if(!memcmp(p,&gateway_fx_params,sizeof *p) || !fx_build(p,&b))return;
    /* Only this small bank copy is protected; coefficient math stays foreground. */
    uint32_t lock=plat_lock();gateway_fx.pending=b;gateway_fx.generation++;gateway_usb_out_publish(&b);plat_unlock(lock);
    gateway_fx_params=*p;gateway_fx_work_capture();gateway_fx_metadata_changed();gateway_fx_notice=0;
    gateway_page_redraw=1;gateway_bt_badge_redraw=1;gateway_lights();
}
static void gateway_fx_save(uint32_t now,int force)
{
    if(gateway_fx.faults!=gateway_fx_fault_seen){gateway_fx_fault_seen=gateway_fx.faults;plat_log("FX numerical recovery");}
    if(gateway_fx_dialog || !gateway_fx_dirty || (!force && (now-gateway_fx_ms<750u || now-gateway_fx_retry_ms<5000u)))return;
    gateway_fx_presets_t r=gateway_fx_presets_saved;
    gateway_fx_metadata(&r);gateway_fx_write(&r); /* Saved slots only: never working edits. */
}
static int gateway_fx_clamp(int n,int lo,int hi){return n<lo?lo:n>hi?hi:n;}
static uint32_t gateway_fx_turn_ms[NE];
static uint8_t gateway_fx_turn_count[NE];
static int gateway_fx_step(unsigned enc,int delta)
{
    uint32_t now=plat_ms(),dt=now-gateway_fx_turn_ms[enc];
    unsigned count=dt<90u?gateway_fx_turn_count[enc]+1u:0;
    if(count>15)count=15;
    gateway_fx_turn_count[enc]=count;gateway_fx_turn_ms[enc]=now;
    return count>=10?30:count>=6?10:count>=3?3:1;
}
static void gateway_fx_turn(unsigned enc,int delta)
{
    if(enc==EN_PRESET){gateway_fx_preset_select(delta);return;}
    if(enc==EN_SELECT){if(!gateway_fx_limiter){gateway_fx_row=1+(gateway_fx_row-1+delta%6+6)%6;gateway_page_redraw=1;}return;}
    delta=gateway_fx_clamp(delta,-2000,2000);
    fx_params_t p=gateway_fx_params;
    int step=gateway_fx_step(enc,delta);
    if(gateway_fx_limiter){
        if(enc==EN_K1)p.input=gateway_fx_clamp(p.input+step*delta,-240,240);
        if(enc==EN_K2){int release=(int)((float)p.release*fm_expf((float)gateway_fx_clamp(delta*step,-1000,1000)*0.01f)+0.5f);
            if(release==p.release)release+=delta>0?1:-1;
            p.release=gateway_fx_clamp(release,10,1000);}
        if(enc==EN_K3)p.ceiling=gateway_fx_clamp(p.ceiling+step*delta,-240,0);

    }
    else if(gateway_fx_row==1){
        if(enc==EN_K1)p.post=gateway_fx_clamp(p.post+step*delta,-240,120);

    }else{
        fx_band_t *b=&p.band[gateway_fx_row-2];
        if(enc==EN_ALGO)b->type=(b->type+delta%FX_TYPES+FX_TYPES)%FX_TYPES;
        if(enc==EN_K1){int hz=(int)((float)b->hz*fm_expf((float)gateway_fx_clamp(delta*step,-2000,2000)*0.0057762265f)+0.5f);
            if(hz==b->hz)hz+=delta>0?1:-1;
            b->hz=gateway_fx_clamp(hz,20,19850);}
        if(enc==EN_K2){
            if(b->type==FX_LOW_PASS || b->type==FX_HIGH_PASS)p.slope[gateway_fx_row-2]=gateway_fx_clamp(p.slope[gateway_fx_row-2]+delta,0,FX_STAGES-1);
            else b->gain=gateway_fx_clamp(b->gain+step*delta,-240,240);
        }
        if(enc==EN_K3)b->q=gateway_fx_clamp((int)b->q+step*delta,10,1800);

    }
    gateway_fx_publish(&p);
}

#include "gui.h"
