/* SPDX-License-Identifier: MPL-2.0
 * Spectrum-only regression: ballistics, peak hold, incremental pixels and
 * actual mode/capture integration. No physical device access. */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>

static uint16_t baseline[240*240];
static uint16_t saved[240*240];
static uint32_t rng = 92347u;
static uint32_t random_next(void) { rng = rng*1664525u+1013904223u; return rng; }
static uint16_t wire(uint16_t p) { return (uint16_t)((p>>8)|(p<<8)); }

/* Independent full-frame reference: every chart pixel is derived from its
 * final bar/peak geometry, rather than the incremental update operations. */
static void pixels(void)
{
    unsigned x, y;
    for (y=0; y<240; y++) for (x=0; x<240; x++) {
        uint16_t want=baseline[y*240+x];
        if (y>=26 && y<162 && x>=28) {
            want=0;
            unsigned b=((x-28+1)*40-1)/212;
            if (x<28+(b+1)*212/40-1) {
                double bottom=-(double)gateway_spectrum_min;
                double span=(gateway_spectrum_min-gateway_spectrum_max);
                int level=(int)lround(((double)gateway_spectrum_level[b]/1000.0-bottom)*136.0/span);
                int peak=(int)lround(((double)gateway_spectrum_peak[b]/1000.0-bottom)*136.0/span);
                if(level<0)level=0;if(level>136)level=136;
                if(peak<0)peak=0;if(peak>136)peak=136;
                if (y>=162-(unsigned)level) want=wire(GATEWAY_SPECTRUM_BAR_COLOR);
                if (peak && y==162-(unsigned)peak) want=wire(GATEWAY_SPECTRUM_PEAK_COLOR);
            }
        }
        if (fb[y*240+x]!=want) {
            fprintf(stderr,"spectrum pixel (%u,%u): got %04x expected %04x\n",x,y,fb[y*240+x],want);
            abort();
        }
    }
}

static void release_schedule(unsigned interval, uint32_t start)
{
    unsigned elapsed=0;
    gateway_spectrum_reset(start);
    gateway_spectrum_target[0]=-6000;
    gateway_spectrum_step(start,0);
    gateway_spectrum_target[0]=GATEWAY_SPECTRUM_FLOOR;
    while (elapsed<2000) {
        unsigned dt=interval;
        if (dt>2000-elapsed) dt=2000-elapsed;
        elapsed+=dt;
        gateway_spectrum_step(start+elapsed,dt);
    }
    assert(gateway_spectrum_level[0]==-54000);
    assert(gateway_spectrum_peak[0]==-21000);
}

static void ballistics(void)
{
    unsigned i;
    gateway_spectrum_reset(100);
    gateway_spectrum_target[0]=-6000;
    gateway_spectrum_step(100,33);
    assert(gateway_spectrum_level[0]==-6000 && gateway_spectrum_peak[0]==-6000);
    gateway_spectrum_target[0]=GATEWAY_SPECTRUM_FLOOR;
    for (i=1;i<=7;i++) gateway_spectrum_step(100+i*100,100);
    gateway_spectrum_step(850,50); /* exactly 750 ms: hold still intact */
    assert(gateway_spectrum_peak[0]==-6000 && gateway_spectrum_level[0]==-24000);
    gateway_spectrum_step(866,16);
    assert(gateway_spectrum_peak[0]==-6192);
    gateway_spectrum_target[0]=-5000;
    gateway_spectrum_step(900,34); /* immediate rise and renewed hold */
    assert(gateway_spectrum_peak[0]==-5000 && gateway_spectrum_peak_at[0]==900);
    gateway_spectrum_target[1]=-12000;
    gateway_spectrum_step(933,33);
    assert(gateway_spectrum_peak_at[1]==933 && gateway_spectrum_peak_at[0]==933);
    gateway_spectrum_target[0]=GATEWAY_SPECTRUM_FLOOR;
    gateway_spectrum_step(966,33);
    assert(gateway_spectrum_peak_at[0]==933 && gateway_spectrum_peak_at[1]==966);
    for (i=1;i<=120;i++) release_schedule(i,1000);
    release_schedule(33,UINT32_MAX-400u);
    gateway_spectrum_reset(0);
    gateway_spectrum_target[0]=0;
    gateway_spectrum_step(0,0);
    gateway_spectrum_target[0]=GATEWAY_SPECTRUM_FLOOR;
    gateway_spectrum_step(1000,1000); /* capped foreground stall */
    assert(gateway_spectrum_level[0]==-6000 && gateway_spectrum_peak[0]==-3000);
    for(i=1;i<=600;i++) gateway_spectrum_step(1000+33*i,33);
    assert(gateway_spectrum_level[0]==GATEWAY_SPECTRUM_FLOOR && gateway_spectrum_peak[0]==GATEWAY_SPECTRUM_FLOOR);
    for(i=0;i<3;i++) {
        gateway_spectrum_rate=(uint8_t)i;
        gateway_spectrum_reset(0);gateway_spectrum_target[0]=-6000;
        gateway_spectrum_step(0,0);gateway_spectrum_target[0]=GATEWAY_SPECTRUM_FLOOR;
        gateway_spectrum_step(200,200);
        const int rates[3]={48,24,12};
        assert(gateway_spectrum_level[0]==-6000-200*rates[i]);
    }
    gateway_spectrum_rate=1;
    puts("PASS: immediate attack, 750-ms hold/retrigger, independent bands, slower releases, 120 frame schedules, timer wrap and silence");
}

static void incremental(void)
{
    unsigned frame,b; uint32_t time=1000,changes;
    gateway_spectrum_reset(time);
    memset(fb,0,sizeof fb);
    gateway_spectrum_paint(1);
    memcpy(baseline,fb,sizeof fb);
    for(frame=0;frame<1000;frame++) {
        uint32_t dt=1u+random_next()%90u;
        time+=dt;
        for(b=0;b<40;b++) {
            if(frame%3==0) gateway_spectrum_target[b]=-(int32_t)(random_next()%150001u);
            if(frame%19==0) gateway_spectrum_target[b]=GATEWAY_SPECTRUM_FLOOR;
        }
        if(frame%100==0) {
            gateway_spectrum_max=(uint8_t)(random_next()%149u);
            gateway_spectrum_min=(uint8_t)(gateway_spectrum_max+1+random_next()%(150-gateway_spectrum_max));
            gateway_spectrum_rate=(uint8_t)((frame/100)%3);
            gateway_spectrum_paint(1);
            memcpy(baseline,fb,sizeof fb);
        }
        gateway_spectrum_step(time,dt);
        gateway_spectrum_paint(0);
        pixels();
        changes=lcd_changes;
        gateway_spectrum_paint(0);
        assert(lcd_changes==changes); /* identical geometry: no LCD traffic */
        if(frame%100==0) {
            memcpy(saved,fb,sizeof fb);
            gateway_spectrum_paint(1);
            assert(!memcmp(saved,fb,sizeof fb));
        }
    }
    gateway_spectrum_reset(time);
    gateway_spectrum_paint(1);
    pixels(); /* all old bars and detached peak lines removed */
    assert(!cv_cuts);
    puts("PASS: 1000 randomized 40-band frames match full pixel reference, peak/bar overlap, gaps/bounds, forced redraw and idle drawing");
}

static void integration(void)
{
    unsigned i;
    app_init();app_ui_init();
    gateway_mode=GATEWAY_MODE_SPECTRUM;gateway_view_changed();
    now_ms=1000;
    for(i=0;i<PLAT_SPECTRUM_FRAMES;i++) {
        usb_input_spectrum_packet_begin();
        usb_input_spectrum_push(i&1 ? 4194304 : -4194304,0);
    }
    /* Use an in-band tone so the top line is actually visible. */
    for(i=0;i<PLAT_SPECTRUM_FRAMES;i++)
        usb_input_spectrum_push((int32_t)(4194304.0*sin(6.283185307179586*1000.0*i/44100.0)),0);
    app_loop();
    assert(gateway_spectrum_peak[22]>-10000 && usb_spectrum_enabled);
    uint32_t changes=lcd_changes;
    app_loop();assert(lcd_changes==changes); /* 33-ms refresh limit */
    gateway_screen=GATEWAY_SETTINGS;gateway_view_changed();app_loop();
    for(i=0;i<40;i++) assert(gateway_spectrum_peak[i]==GATEWAY_SPECTRUM_FLOOR && gateway_spectrum_level[i]==GATEWAY_SPECTRUM_FLOOR);
    assert(!usb_spectrum_enabled);
    gateway_screen=GATEWAY_MAIN;gateway_view_changed();app_loop();
    assert(usb_spectrum_enabled);
    for(i=0;i<40;i++) assert(gateway_spectrum_peak[i]==GATEWAY_SPECTRUM_FLOOR && gateway_spectrum_target[i]==GATEWAY_SPECTRUM_FLOOR);
    assert(!cpu_work_depth && !cv_cuts);
    puts("PASS: actual spectrum page renders peak lines, honors refresh limit, clears peaks on exit/re-entry");
}

int main(void)
{
    palette_set(APP_PALETTE);
    ballistics();incremental();integration();
}
