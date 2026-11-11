/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
int main(void)
{
    palette_set(APP_PALETTE);now_ms=1000;app_init();app_ui_init();
    gateway_bt_mode=btr_host_on=btr_host_connected=1;
    btr_host_codec=PLAT_BT_CODEC_AAC;gateway_fx_params.chain=1;gateway_usb_out_mode=1;
    cpu_work_window=cpu2_work_window=plat_ticks24();cpu_work_pct=cpu2_work_percent=100;
    gateway_bar_key=UINT32_MAX;gateway_draw_cpu();
    unsigned badges_end=3+text_w(&FONT_T,"USB IN")+3+2+text_w(&FONT_T,"BT AAC")+3+2+text_w(&FONT_T,"FX")+3+2+text_w(&FONT_T,"USB OUT")+3;
    unsigned text_start=237-text_w(&FONT_T,"CPU 100/100% BAT 100%");
    printf("top bar: badges end %u, CPU/battery start %u\n",badges_end,text_start);
    fflush(stdout);
    assert(badges_end+2<=text_start);
    /* Only the second core changes: the cached status bar must repaint. */
    uint32_t key=gateway_bar_key;cpu2_work_percent=20;gateway_draw_cpu();assert(gateway_bar_key!=key);
    assert(gateway_cpu_drawn==100 && plat_cpu2_work_pct()==20 && gateway_battery_mode);
    /* Host ADC 620 maps to an estimated 99%; layout above checks 100%. */
    cv_begin(240,16,C_BLACK);cv_text(237-text_w(&FONT_T,"CPU 100/20% BAT 99%"),2,&FONT_T,"CPU 100/20% BAT 99%",C_WHITE);
    for(unsigned y=0;y<16;y++)for(unsigned x=text_start;x<240;x++)assert(fb[y*240+x]==cv_px[y*240+x]);
    gateway_cpu_mode=0;gateway_draw_cpu();key=gateway_bar_key;
    cpu2_work_percent=30;gateway_draw_cpu();assert(key==gateway_bar_key);
    unsigned fx_x=3+text_w(&FONT_T,"USB IN")+3+2+text_w(&FONT_T,"BT AAC")+3+2;
    for(unsigned chain=0;chain<2;chain++)for(unsigned eq=0;eq<2;eq++)for(unsigned lim=0;lim<2;lim++){
        gateway_fx_params.chain=chain;gateway_fx_params.eq=eq;gateway_fx_params.lim=lim;
        gateway_draw_cpu();
        uint16_t expected=!chain?C_GRAY:eq||lim?RGB(20,150,50):RGB(190,150,15);
        assert(fb[2*240+fx_x]==(uint16_t)((expected>>8)|(expected<<8)));
        unsigned width=text_w(&FONT_T,"FX")+3;
        if(!chain)assert(!fb[4*240+fx_x+1]);
        else {
            /* Exact badge pixels also check black lettering on yellow fill. */
            static uint16_t actual[240*16];memcpy(actual,fb,sizeof actual);
            cv_begin(240,16,C_BLACK);gateway_badge(fx_x,width,"FX",expected,0);cv_blit(0,0);
            for(unsigned y=0;y<16;y++)for(unsigned x=fx_x;x<fx_x+width;x++)assert(actual[y*240+x]==fb[y*240+x]);
            memcpy(fb,actual,sizeof actual);
        }
    }
    gateway_fx_params.chain=1;gateway_fx_params.eq=0;gateway_fx_params.lim=1;gateway_draw_cpu();
    key=gateway_bar_key;unsigned draws=blits;gateway_fx_params.eq=1;gateway_draw_cpu();
    assert(key==gateway_bar_key && draws==blits); /* still green, no redundant repaint */
    gateway_fx_params.eq=gateway_fx_params.lim=0;gateway_draw_cpu();assert(key!=gateway_bar_key && blits>draws);
    puts("PASS: all 8 chain/EQ/limiter badge states, yellow with both bypassed, black text, gray global bypass and cache transitions");
    assert(!cv_cuts);
    puts("PASS: CPU0/CPU1 text, independent CPU2 redraw and CPU-off setting without badge/battery overlap");
}
