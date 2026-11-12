/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static uint16_t base[240*240],expected[240*240];
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();app_loop();}
static void equal(const uint16_t *want,unsigned page,const char *stage)
{
    for(unsigned i=0;i<240*240;i++)if(want[i]!=fb[i]){
        fprintf(stderr,"page %u %s at %u,%u: expected %04x got %04x (base %04x)\n",page,stage,i%240,i/240,swap16(want[i]),swap16(fb[i]),swap16(base[i]));
        assert(want[i]==fb[i]);
    }
}
static void reference(unsigned state)
{
    memcpy(expected,base,sizeof expected);
    int left=240,right=0,top=240,bottom=0;
    for(unsigned row=0;row<240;row+=16){
        cv_begin(240,16,C_BLACK);
        cv_text(gateway_midi_overlay.text_x,gateway_midi_overlay.text_y-(int)row,&FONT_L,"MIDI",C_WHITE);
        for(unsigned y=0;y<16;y++)for(unsigned x=0;x<240;x++)if(cv_px[y*240+x]){
            if((int)x<left)left=x;if((int)x>right)right=x;
            if((int)(row+y)<top)top=row+y;if((int)(row+y)>bottom)bottom=row+y;
        }
    }
    assert(left-gateway_midi_overlay.x==10 && top-gateway_midi_overlay.y==10);
    assert(gateway_midi_overlay.x+gateway_midi_overlay.w-right-1==10);
    assert(gateway_midi_overlay.y+gateway_midi_overlay.h-bottom-1==10);
    assert(left+right==239 && top+bottom>=319 && top+bottom<=320);
    static const unsigned cell_row[]={0,1,1,2,2,2,2},cell_col[]={2,2,1,0,1,2,3};
    /* Independent fixed layout, 10% scene / 90% black blend, and font renderer. */
    for(unsigned i=state==2?NE+1:0;i<=(state==2?NE+1:NE);i++){
        unsigned bx,by,bw,bh;
        if(i==NE+1){bw=text_w(&FONT_XS,"DISCONNECTED")+20;bx=(240-bw)/2;by=125;bh=70;}
        else if(i==NE){bx=left-10;by=top-10;bw=right-left+21;bh=bottom-top+21;}
        else{bx=34+44*cell_col[i];by=60+26*cell_row[i];bw=40;bh=24;}
        for(unsigned y=by;y<by+bh;y++)for(unsigned x=bx;x<bx+bw;x++){
            unsigned c=swap16(base[y*240+x]);
            unsigned r=(c>>11)/10,g=((c>>5)&63)/10,b=(c&31)/10;
            expected[y*240+x]=swap16((r<<11)|(g<<5)|b);
        }
    }
    for(unsigned row=0;row<240;row+=16){
        cv_begin(240,16,C_BLACK);memcpy(cv_px,expected+row*240,16*240*sizeof(uint16_t));
        if(state==2){
            cv_text((240-text_w(&FONT_L,"USB"))/2,129-(int)row,&FONT_L,"USB",RGB(255,48,48));
            cv_text((240-text_w(&FONT_XS,"DISCONNECTED"))/2,168-(int)row,&FONT_XS,"DISCONNECTED",RGB(255,48,48));
        }else{
            cv_text(gateway_midi_overlay.text_x,gateway_midi_overlay.text_y-(int)row,&FONT_L,"MIDI",RGB(255,48,48));
            for(unsigned e=0;e<NE;e++){
                char number[4];snprintf(number,sizeof number,"%u",gateway_midi_values[e]);
                cv_text(34+44*cell_col[e]+(40-text_w(&FONT_XS,number))/2,
                    60+26*cell_row[e]+(24-FONT_XS.h)/2-(int)row,&FONT_XS,number,C_WHITE);
            }
        }
        memcpy(expected+row*240,cv_px,16*240*sizeof(uint16_t));
    }
}
static void composition(void)
{
    /* Setting enabled alone is not the active REC controller mode. */
    gateway_midi_mode=1;
    for(unsigned y=0;y<240;y+=16){cv_begin(240,16,RGB(100,150,200));cv_blit(0,y);}
    memcpy(base,fb,sizeof base);reference(1);assert(!memcmp(base,fb,sizeof base));
    gateway_midi_active=1;
    for(unsigned pass=0;pass<3;pass++){
        for(unsigned y=0;y<240;y+=16){cv_begin(240,16,RGB(100,150,200));cv_blit(0,y);}
        assert(!memcmp(expected,fb,sizeof expected)); /* No cumulative darkening. */
    }
    unsigned changes=lcd_changes;uint32_t saved_w=cv_w,saved_h=cv_h;
    cv_begin(80,12,RGB(100,150,200));cv_oy=-100;cv_y0=3;cv_y1=10;
    cv_blit_from(80,150,4);
    assert(cv_w==80 && cv_h==12 && cv_oy==-100 && cv_y0==3 && cv_y1==10);
    assert(lcd_changes==changes+1 && !memcmp(expected,fb,sizeof expected));
    assert(saved_w==240 && saved_h==16 && !cv_cuts);
    static const struct {unsigned x,y,w,h;uint16_t color;} fills[]={
        {96,26,4,136,RGB(30,210,100)}, {100,149,10,1,C_WHITE},
        {96,26,4,136,C_BLACK}, {72,138,8,32,RGB(255,210,40)}
    };
    for(unsigned i=0;i<sizeof fills/sizeof *fills;i++){
        for(unsigned y=fills[i].y;y<fills[i].y+fills[i].h;y++)
            for(unsigned x=fills[i].x;x<fills[i].x+fills[i].w;x++)base[y*240+x]=swap16(fills[i].color);
        reference(1);
        gateway_display_fill(fills[i].x,fills[i].y,fills[i].w,fills[i].h,fills[i].color);
        equal(expected,0,"solid fill");
    }
    gateway_midi_active=0;
}
static void pages(void)
{
    static const unsigned rows[]={60,70,90,100,120,130,150,160,180,194};
    for(unsigned page=0;page<9;page++){
        gateway_screen=page<3?GATEWAY_MAIN:page==3 || page==8?GATEWAY_SETTINGS:page==4?GATEWAY_MIXER:GATEWAY_FX;
        gateway_mode=page<3?page:GATEWAY_MODE_METERS;gateway_fx_limiter=page==6;
        gateway_fx_dialog=page==7;gateway_codec_dialog=page==8;
        gateway_view_changed();app_loop();app_frame();gateway_shot_paint();
        memcpy(base,fb,sizeof base);reference(1);tap(B_REC);assert(gateway_midi_active);
        equal(expected,page,"active");
        /* One-, two- and three-digit updates clear old ink in only dirty cells. */
        static const uint8_t values[]={0,127,64};
        for(unsigned v=0;v<3;v++){
            memset(gateway_midi_values,values[v],NE);reference(1);app_loop();equal(expected,page,"values");
        }
        for(unsigned e=0;e<NE;e++){
            gateway_midi_values[e]=5+19*e;reference(1);app_loop();equal(expected,page,"single cell");
        }
        const char *preview=getenv("FM1_MIDI_PREVIEW");if(!page && preview)png_write(preview);
        unsigned changes=lcd_changes;
        for(unsigned i=0;i<sizeof rows/sizeof *rows;i++){
            unsigned row=rows[i];plat_screenshot_begin(row);gateway_shot_paint();plat_screenshot_end();
            for(unsigned x=0;x<240;x++)assert(plat_screenshot_pixel(x)==swap16(expected[row*240+x]));
        }
        assert(lcd_changes==changes); /* Screenshot replay never writes the panel. */
        app_loop();changes=lcd_changes;for(unsigned i=0;i<5;i++)app_loop();assert(lcd_changes==changes);
        tap(B_REC);assert(!gateway_midi_active);equal(base,page,"inactive");
    }
    assert(!cv_cuts && !cpu_work_depth);
}
static void disconnected(void)
{
    gateway_fx_dialog=gateway_codec_dialog=0;gateway_screen=GATEWAY_MAIN;gateway_mode=GATEWAY_MODE_METERS;
    hsx_session=0;gateway_view_changed();app_loop();app_frame();gateway_shot_paint();memcpy(base,fb,sizeof base);
    unsigned writes=store_writes;n_out=0;uint32_t start=now_ms;
    tap(B_REC);assert(!gateway_midi_active && gateway_midi_usb_error && !n_out && store_writes==writes);
    reference(2);equal(expected,0,"USB disconnected");
    static const unsigned phase[]={0,149,150,299,300,449,450,999};
    for(unsigned i=0;i<sizeof phase/sizeof *phase;i++){
        now_ms=start+phase[i];app_loop();
        int bright=phase[i]<150 || (phase[i]>=300 && phase[i]<450);
        assert(gateway_midi_usb_error && !!(lit_btn&(1u<<B_REC))==bright);
        assert(!!(dim_btn&(1u<<B_REC))==!bright);
    }
    /* Held REC has no repeated edge. */
    held_btn=1u<<B_REC;gateway_buttons=held_btn;app_input_only();held_btn=0;app_input_only();
    assert(gateway_midi_usb_error_ms==start);
    now_ms=start+1000;app_loop();assert(!gateway_midi_usb_error && !gateway_midi_active);equal(base,0,"error removed");
    now_ms=UINT32_MAX-500u;tap(B_REC);start=now_ms;
    now_ms=start+999u;app_loop();assert(gateway_midi_usb_error);
    now_ms=start+1000u;app_loop();assert(!gateway_midi_usb_error);
    /* A real second press restarts the one-second message. Replug allows entry. */
    tap(B_REC);start=now_ms;now_ms+=500;tap(B_REC);assert(gateway_midi_usb_error_ms==start+500);
    hsx_session=++hsx_plugs;tap(B_REC);assert(gateway_midi_active && !gateway_midi_usb_error);
    tap(B_REC);assert(!gateway_midi_active && !cv_cuts && !cpu_work_depth);
    puts("PASS: disconnected REC rejects MIDI entry, two bright pulses, one-second two-line overlay, clean removal, held-edge/retry/replug and timer wrap");
}
int main(void)
{
    palette_set(APP_PALETTE);app_init();app_ui_init();app_loop();app_frame();
    composition();pages();disconnected();
    puts("PASS: MIDI overlay uses limiter FONT_L at x=120/y=160, exact 10px padding, red text and 10% transparent black; smaller values in padded 3x4 grid shifted up 50px; fresh/partial strips and solid bars preserve clips and never accumulate opacity");
    puts("PASS: active REC overlay and clean removal on meters/waveform/spectrum/Settings/Mixer/EQ/limiter/both dialogs, no extra idle redraw or canvas cuts, screenshot rows equal LCD");
}
