/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static uint16_t saved[240*240];
static uint32_t seed = 71;
static uint32_t random_word(void) { return seed = seed*1664525u + 1013904223u; }
static void reference_check(void)
{
    unsigned ch,x,y;
    for(ch=0;ch<2;ch++) {
        for(y=0;y<GATEWAY_WAVE_H;y++)for(x=0;x<228;x++) {
            int top=35-(int32_t)gateway_wave[x][2*ch+1]*35/32768;
            int bottom=35-(int32_t)gateway_wave[x][2*ch]*35/32768;
            if(top<0)top=0;if(top>71)top=71;
            if(bottom<0)bottom=0;if(bottom>71)bottom=71;
            uint16_t expected=y>=(unsigned)top && y<=(unsigned)bottom?swap16(RGB(45,130,255)):0;
            assert(fb[(34+78*ch+y)*240+6+x]==expected);
        }
    }
}
int main(void)
{
    palette_set(APP_PALETTE);
    unsigned f,x,ch,k, updates=0;
    uint32_t worst=0,total=0;
    app_splash();
    { const unsigned rows[][2]={{75,107},{111,143},{153,165}};
      for(k=0;k<3;k++) {
        unsigned lo=240,hi=0,n=0,y;
        for(y=rows[k][0];y<rows[k][1];y++)for(x=0;x<240;x++)if(fb[y*240+x]) {
            if(x<lo)lo=x;if(x>hi)hi=x;n++;
        }
        assert(n>20 && lo+hi>=235 && lo+hi<=243);
      }
    }
    app_init(); app_ui_init(); gateway_mode=GATEWAY_MODE_FULL;gateway_view_changed(); gateway_draw(); gateway_draw_meter(1);
    memcpy(saved,fb,sizeof(saved));
    /* Sparse edits exercise crop edges and connector pixels outside the changed
     * column. Mode changes, impulses and silence must erase the previous trace. */
    for(f=0;f<400;f++) {
        if(!(f%19))gateway_zoom=(f%38)?11:0;
        for(k=0;k<1+f%GATEWAY_WAVE_W;k++) {
            x=random_word()%GATEWAY_WAVE_W;
            for(ch=0;ch<2;ch++) {
                int16_t a=(int16_t)random_word(),b=(int16_t)random_word();
                gateway_wave[x][2*ch]=a<b?a:b;
                gateway_wave[x][2*ch+1]=a>b?a:b;
            }
        }
        {uint32_t pixels=gateway_draw_wave(0); if(pixels>worst)worst=pixels; total+=pixels;}
        reference_check();
        for(x=0;x<240*240;x++) {
            unsigned row=x/240,col=x%240;
            if(col>=6 && col<6+GATEWAY_WAVE_W && ((row>=34 && row<34+GATEWAY_WAVE_H) ||
                 (row>=112 && row<112+GATEWAY_WAVE_H)))continue;
            assert(saved[x]==fb[x]);
        }
    }
    assert(worst<=2*GATEWAY_WAVE_W*GATEWAY_WAVE_H);
    assert(gateway_draw_wave(0)==0);
    for(x=0;x<GATEWAY_WAVE_W;x++)for(k=0;k<4;k++)gateway_wave[x][k]=0;
    gateway_draw_wave(0);reference_check();assert(gateway_draw_wave(0)==0);
    {uint32_t before=blits;gateway_draw_meter(0);gateway_draw_meter(0);assert(blits==before);}
    /* Main-loop scheduling: configured interval between captures, including zoom edits,
     * irregular loop timing and timestamp wrap. No catch-up bursts. */
    now_ms=1000;app_ui_init(); gateway_mode=GATEWAY_MODE_FULL;gateway_view_changed();app_loop();
    {uint32_t previous=gateway_wave_ms;
      for(f=1001;f<=2000;f++) {
        now_ms=f;gateway_redraw=1;app_loop();
        if(gateway_wave_ms!=previous) {assert(gateway_wave_ms-previous>=GATEWAY_WAVE_INTERVAL);updates++;previous=gateway_wave_ms;}
      }
    }
    assert(updates==1000u/GATEWAY_WAVE_INTERVAL);
    now_ms=4294967280u;app_ui_init(); gateway_mode=GATEWAY_MODE_FULL;gateway_view_changed();app_loop();now_ms=18;app_loop();assert(gateway_wave_ms==18);
    {char label[12];const uint32_t loads[]={0,9,10,99,100,999,1000};
     const char *expected[]={"0%","9%","10%","99%","100%","999%","999%"};
     for(k=0;k<7;k++){gateway_cpu_label(label,loads[k]);assert(!strcmp(label,expected[k]));}
     now_ms=3000;cpu_work_pct=47;cpu_work_window=plat_ticks24();app_frame();assert(gateway_cpu_drawn==47);
    }
    assert(cv_cuts==0);
    printf("PASS: 400 incremental frames match full redraw pixel-for-pixel; max %u pixels/frame vs old 43200; static wave/meter skip; %u captures/1000 ms, configured refresh interval, CPU format/refresh\n",worst,updates);
}
