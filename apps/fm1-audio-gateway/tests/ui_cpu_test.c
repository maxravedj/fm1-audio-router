/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <limits.h>
static uint16_t saved[240*240];
static uint32_t rng=197;
static uint32_t random_word(void){return rng=rng*1664525u+1013904223u;}
static uint32_t old_wave_pixels(void)
{
    uint32_t pixels=0;
    for(unsigned ch=0;ch<2;ch++) {
        unsigned first=GATEWAY_WAVE_W,last=0,top=GATEWAY_WAVE_H,bottom=0;
        for(unsigned x=0;x<GATEWAY_WAVE_W;x++) {
            unsigned a=gateway_wave_y(gateway_wave[x][2*ch+1]),b=gateway_wave_y(gateway_wave[x][2*ch]);
            if(a!=gateway_plot_drawn[ch][x][0] || b!=gateway_plot_drawn[ch][x][1]){if(first==GATEWAY_WAVE_W)first=x;last=x;}
        }
        if(first==GATEWAY_WAVE_W)continue;
        for(unsigned x=first;x<=last;x++) {
            unsigned a=gateway_wave_y(gateway_wave[x][2*ch+1]),b=gateway_wave_y(gateway_wave[x][2*ch]);
            if(a<top)top=a;if(gateway_plot_drawn[ch][x][0]<top)top=gateway_plot_drawn[ch][x][0];
            if(b>bottom)bottom=b;if(gateway_plot_drawn[ch][x][1]>bottom)bottom=gateway_plot_drawn[ch][x][1];
        }
        pixels+=(last-first+1)*(bottom-top+1);
    }
    return pixels;
}
static void waveform(void)
{
    gateway_mode=GATEWAY_MODE_FULL;gateway_monitor=PLAT_MONITOR_MASTER;gateway_master_wave_auto_scale=0;
    gateway_view_changed();gateway_capture_wave(now_ms);gateway_draw_wave(1);
    const unsigned scales[]={32,33,71,1024,4000,32768};
    for(unsigned i=0;i<sizeof scales/sizeof *scales;i++) {
        gateway_wave_scale=scales[i];gateway_wave_multiplier=(35u<<20)/scales[i];
        for(int v=INT16_MIN;v<=INT16_MAX;v++) {
            int64_t product=(int64_t)v*gateway_wave_multiplier;
            int y=35-(product>=0?product>>20:-((-product)>>20));
            if(y<0)y=0;if(y>71)y=71;assert(gateway_wave_y((int16_t)v)==y);
        }
    }
    gateway_wave_scale=32768;gateway_wave_multiplier=1120;
    unsigned old_total=0,new_total=0;
    for(unsigned f=0;f<300;f++) {
        for(unsigned x=0;x<GATEWAY_WAVE_W;x++)for(unsigned ch=0;ch<2;ch++) {
            /* Broad, changing compressed envelopes with fixed scale. */
            gateway_wave[x][2*ch]=(int16_t)(-24000-(int)(random_word()%1000));
            gateway_wave[x][2*ch+1]=(int16_t)(24000+(random_word()%1000));
        }
        unsigned before=old_wave_pixels(),after=gateway_draw_wave(0);
        assert(after<=before);old_total+=before;new_total+=after;
        memcpy(saved,fb,sizeof saved);gateway_draw_wave(1);assert(!memcmp(saved,fb,sizeof saved));
    }
    assert(new_total*5u<old_total && gateway_draw_wave(0)==0);
    printf("PASS: waveform fixed-scale envelopes: %u vs %u old LCD pixels, %.1f%% fewer; exact full-redraw pixels and int16 quantization\n",new_total,old_total,100.0*(old_total-new_total)/old_total);
}
/* Frozen .40 global edge bounds, counting pixels and canvas transfers. */
static uint32_t previous_edges(unsigned *transfers)
{
    uint32_t pixels=0;*transfers=0;
    for(unsigned ch=0;ch<2;ch++) {
        unsigned first[2]={GATEWAY_WAVE_W,GATEWAY_WAVE_W},last[2]={0,0};
        unsigned top[2]={GATEWAY_WAVE_H,GATEWAY_WAVE_H},bottom[2]={0,0};
        for(unsigned x=0;x<GATEWAY_WAVE_W;x++) {
            unsigned t=gateway_wave_y(gateway_wave[x][2*ch+1]),b=gateway_wave_y(gateway_wave[x][2*ch]);
            unsigned ot=gateway_plot_drawn[ch][x][0],ob=gateway_plot_drawn[ch][x][1];
            for(unsigned e=0;e<2;e++) {
                if((!e && t==ot)||(e && b==ob))continue;
                unsigned a=e?((b<ob?b:ob)+1):(t<ot?t:ot),z=e?(b>ob?b:ob):((t>ot?t:ot)-1);
                if(x<first[e])first[e]=x;last[e]=x;if(a<top[e])top[e]=a;if(z>bottom[e])bottom[e]=z;
            }
        }
        if(first[0]<GATEWAY_WAVE_W && first[1]<GATEWAY_WAVE_W && bottom[0]+1>=top[1] && bottom[1]+1>=top[0]) {
            if(first[1]<first[0])first[0]=first[1];if(last[1]>last[0])last[0]=last[1];
            if(top[1]<top[0])top[0]=top[1];if(bottom[1]>bottom[0])bottom[0]=bottom[1];first[1]=GATEWAY_WAVE_W;
        }
        for(unsigned e=0;e<2;e++)if(first[e]<GATEWAY_WAVE_W) {
            unsigned w=last[e]-first[e]+1,h=bottom[e]-top[e]+1,rows=APP_CANVAS_PX/w;
            pixels+=w*h;*transfers+=(h+rows-1)/rows;
        }
    }
    return pixels;
}
static void automatic(void)
{
    gateway_screen=GATEWAY_MAIN;gateway_mode=GATEWAY_MODE_FULL;gateway_monitor=PLAT_MONITOR_MASTER;
    gateway_master_wave_auto_scale=1;gateway_zoom=11;gateway_view_changed();gateway_draw_wave(1);
    gateway_wave_auto_seen=1;gateway_wave_scale=32768; /* simulate turning MASTER down */
    uint64_t old_cost=0,new_cost=0;unsigned old_late=0,new_late=0;
    for(unsigned f=0;f<600;f++) {
        int32_t pcm[2*704];
        for(unsigned i=0;i<704;i++)for(unsigned ch=0;ch<2;ch++) {
            double phase=(double)(f*704+i)*(ch?0.29:0.17);
            pcm[2*i+ch]=(int32_t)(512*sin(phase))*256;
        }
        master_meter_samples(pcm,704);now_ms+=16;gateway_capture_wave(now_ms);
        unsigned calls;unsigned before=previous_edges(&calls),blit_start=blits;
        unsigned after=gateway_draw_wave(0),new_calls=blits-blit_start;
        old_cost+=before+24u*calls;new_cost+=after+24u*new_calls;
        if(f>=500){old_late+=before;new_late+=after;}
        memcpy(saved,fb,sizeof saved);gateway_draw_wave(1);assert(!memcmp(saved,fb,sizeof saved));
    }
    assert(gateway_wave_scale<650 && new_cost<old_cost*3/4 && new_late<old_late*3/4);
    printf("PASS: actual MASTER auto-scale ramp capture, 600 exact pixel frames, late pixels %u/%u; command-weighted transfer cost %llu/%llu vs .40\n",new_late,old_late,(unsigned long long)new_cost,(unsigned long long)old_cost);
    /* Widely separated sparse samples must not repaint the intervening gap. */
    gateway_wave_scale=32768;gateway_wave_multiplier=1120;
    for(unsigned n=0;n<100;n++) {
        for(unsigned x=0;x<GATEWAY_WAVE_W;x++)for(unsigned ch=0;ch<2;ch++) {
            int16_t v=(int16_t)random_word();gateway_wave[x][2*ch]=gateway_wave[x][2*ch+1]=v;
        }
        unsigned calls,before=previous_edges(&calls),start=blits,after=gateway_draw_wave(0);
        assert(after+24*(blits-start)<before+24*calls);
        memcpy(saved,fb,sizeof saved);gateway_draw_wave(1);assert(!memcmp(saved,fb,sizeof saved));
    }
    puts("PASS: sparse/disjoint full-amplitude traces retain exact pixels with lower command-weighted transfer cost");
}
static void limiter(void)
{
    gateway_screen=GATEWAY_FX;gateway_fx_limiter=1;gateway_fx_params.lim=0;
    gateway_fx_view();gateway_fx_draw();
    unsigned before=blits;
    for(unsigned f=0;f<200;f++){now_ms+=48;gateway_fx_status();}
    assert(blits==before); /* Disabled silent view makes no LCD transfers. */
    for(unsigned active=0;active<2;active++) {
        gateway_fx_params.lim=active;gateway_fx_draw();
        for(unsigned f=0;f<100;f++) {
            gateway_fx.output_peak[0]=random_word()%8388608;
            gateway_fx.output_peak[1]=random_word()%8388608;
            gateway_fx.meter_reduction=1u+random_word()%65536;
            now_ms+=48;gateway_fx_status();memcpy(saved,fb,sizeof saved);
            gateway_fx_limiter_region(0,34,240,206,1);assert(!memcmp(saved,fb,sizeof saved));
        }
        for(unsigned f=0;f<180;f++){now_ms+=48;gateway_fx_status();}
        assert(!gateway_fx_reduction && !gateway_fx_meter[0].held && !gateway_fx_meter[1].held);
        before=blits;for(unsigned f=0;f<100;f++){now_ms+=48;gateway_fx_status();}assert(before==blits);
    }
    puts("PASS: limiter idle sends zero LCD pixels enabled/bypassed; 200 changing/releasing meter frames match full redraw, static controls retained");
}
int main(void)
{
    palette_set(APP_PALETTE);app_init();app_ui_init();waveform();automatic();limiter();assert(!cv_cuts);
}
