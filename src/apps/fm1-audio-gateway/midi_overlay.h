/* SPDX-License-Identifier: MPL-2.0
 * Foreground composition only: no backing bitmap, allocation or extra refresh.
 * Match the limiter's FONT_L, with 10 px padding around the actual ink bounds. */
static struct { int16_t x,y,w,h,text_x,text_y; uint8_t seen,values[NE]; } gateway_midi_overlay;
static struct {unsigned x,y,w,h;uint8_t active;} gateway_midi_clip;
static unsigned gateway_midi_overlay_state(void){return gateway_midi_usb_error?2u:gateway_midi_active?1u:0u;}
enum { MIDI_CELL_W=40, MIDI_CELL_H=24, MIDI_GRID_X=34, MIDI_GRID_Y=60, MIDI_COL_STEP=44, MIDI_ROW_STEP=26,
    MIDI_ERROR_Y=125, MIDI_ERROR_H=70 };
static const uint8_t gateway_midi_cells[NE][2]={
    {0,2}, {1,2}, {1,1}, {2,0}, {2,1}, {2,2}, {2,3}
};
static void gateway_midi_rect(unsigned i,unsigned *x,unsigned *y,unsigned *w,unsigned *h)
{
    if(i==NE+1){*w=text_w(&FONT_XS,"DISCONNECTED")+20;*x=(240-*w)/2;*y=MIDI_ERROR_Y;*h=MIDI_ERROR_H;}
    else if(i==NE){*x=gateway_midi_overlay.x;*y=gateway_midi_overlay.y;*w=gateway_midi_overlay.w;*h=gateway_midi_overlay.h;}
    else{*x=MIDI_GRID_X+MIDI_COL_STEP*gateway_midi_cells[i][1];*y=MIDI_GRID_Y+MIDI_ROW_STEP*gateway_midi_cells[i][0];*w=MIDI_CELL_W;*h=MIDI_CELL_H;}
}
static void gateway_midi_overlay_init(void)
{
    const felucca_font_t *f=&FONT_L;
    int left=240,top=240,right=0,bottom=0,pen=0;
    for(const char *s="MIDI";*s;s++){
        unsigned g=glyph(f,(uint8_t)*s);
        int x=pen-f->pad,y=0,h=f->h;
#ifdef FONT_TRIM
        const uint8_t *d=f->data+f->off[g];x+=d[0];y=d[1];h=d[2];
#endif
        if(x<left)left=x;if(y<top)top=y;
        if(x+f->bw[g]>right)right=x+f->bw[g];if(y+h>bottom)bottom=y+h;
        pen+=f->adv[g];
    }
    int w=right-left,h=bottom-top;
    gateway_midi_overlay.text_x=(240-w)/2-left;
    gateway_midi_overlay.text_y=160-h/2-top;
    gateway_midi_overlay.x=(240-w)/2-10;gateway_midi_overlay.y=160-h/2-10;
    gateway_midi_overlay.w=w+20;gateway_midi_overlay.h=h+20;gateway_midi_overlay.seen=0;
    gateway_midi_clip.active=0;
}
static void gateway_midi_number(char text[4],unsigned value)
{
    unsigned n=0;
    if(value>=100)text[n++]='0'+value/100;
    if(value>=10)text[n++]='0'+value/10%10;
    text[n++]='0'+value%10;text[n]=0;
}
int app_canvas_overlay(uint32_t x,uint32_t y,uint32_t first_row)
{
    unsigned left=x,top=y+first_row,right=x+cv_w,bottom=y+cv_h;
    if(gateway_midi_clip.active){
        if(left<gateway_midi_clip.x)left=gateway_midi_clip.x;
        if(top<gateway_midi_clip.y)top=gateway_midi_clip.y;
        if(right>gateway_midi_clip.x+gateway_midi_clip.w)right=gateway_midi_clip.x+gateway_midi_clip.w;
        if(bottom>gateway_midi_clip.y+gateway_midi_clip.h)bottom=gateway_midi_clip.y+gateway_midi_clip.h;
    }
    if(left>=right || top>=bottom)return gateway_midi_clip.active;
    int32_t oy=cv_oy,y_start=cv_y0,y_end=cv_y1;
    cv_oy=0;cv_y0=top-y;cv_y1=bottom-y;
    unsigned state=gateway_midi_overlay_state(),first=state==2?NE+1u:0u,last=state==2?NE+1u:NE;
    if(gateway_ui_ready && state && x<240 && y<240)for(unsigned i=first;i<=last;i++){
        unsigned bx,by,bw,bh;gateway_midi_rect(i,&bx,&by,&bw,&bh);
        unsigned x0=left>bx?left:bx,y0=top>by?top:by;
        unsigned x1=right<bx+bw?right:bx+bw,y1=bottom<by+bh?bottom:by+bh;
        if(x0>=x1 || y0>=y1)continue;
        /* 10% scene / 90% black, component-wise RGB565. */
        for(unsigned row=y0;row<y1;row++)for(unsigned col=x0;col<x1;col++){
            uint16_t *p=cv_px+(row-y)*cv_w+col-x;unsigned c=swap16(*p);
            *p=swap16(((c>>11)/10u<<11) | (((c>>5&63u)/10u)<<5) | (c&31u)/10u);
        }
        if(i==NE+1){
            cv_text((240-text_w(&FONT_L,"USB"))/2-(int)x,(int)by+4-(int)y,&FONT_L,"USB",RGB(255,48,48));
            cv_text((240-text_w(&FONT_XS,"DISCONNECTED"))/2-(int)x,(int)by+43-(int)y,&FONT_XS,"DISCONNECTED",RGB(255,48,48));
        }
        else if(i==NE)cv_text(gateway_midi_overlay.text_x-(int)x,gateway_midi_overlay.text_y-(int)y,&FONT_L,"MIDI",RGB(255,48,48));
        else{
            char number[4];gateway_midi_number(number,gateway_midi_values[i]);
            cv_text((int)bx+((int)bw-text_w(&FONT_XS,number))/2-(int)x,
                (int)by+((int)bh-FONT_XS.h)/2-(int)y,&FONT_XS,number,C_WHITE);
        }
    }
    cv_oy=oy;cv_y0=y_start;cv_y1=y_end;
    if(!gateway_midi_clip.active)return 0;
    /* Compact only the requested patch in the existing canvas. No backing bitmap. */
    unsigned w=right-left,h=bottom-top;
    for(unsigned row=0;row<h;row++){
        uint16_t *dst=cv_px+row*w,*src=cv_px+(top-y+row)*cv_w+left-x;
        /* Destination always precedes source; preserve overlap without libc. */
        for(unsigned col=0;col<w;col++)dst[col]=src[col];
    }
    lcd_blit(left,top,w,h,cv_px);return 1;
}
static void gateway_display_fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t c)
{
    if(!w || !h || x>=240 || y>=240)return;
    if(w>240-x)w=240-x;if(h>240-y)h=240-y;
    int composite=gateway_midi_clip.active;
    unsigned state=gateway_midi_overlay_state(),first=state==2?NE+1u:0u,last=state==2?NE+1u:NE;
    if(!composite && gateway_ui_ready && state)for(unsigned i=first;i<=last;i++){
        unsigned bx,by,bw,bh;gateway_midi_rect(i,&bx,&by,&bw,&bh);
        if(x<bx+bw && y<by+bh && x+w>bx && y+h>by){composite=1;break;}
    }
    if(!composite){
        lcd_fill(x,y,w,h,c);return;
    }
    if(gateway_midi_clip.active && (x+w<=gateway_midi_clip.x || y+h<=gateway_midi_clip.y ||
       x>=gateway_midi_clip.x+gateway_midi_clip.w || y>=gateway_midi_clip.y+gateway_midi_clip.h))return;
    /* Spectrum's fast solid fills need composition only where they overlap. */
    for(unsigned row=0;row<h;){
        unsigned n=h-row;if(n>APP_CANVAS_PX/w)n=APP_CANVAS_PX/w;
        cv_begin(w,n,c);cv_blit(x,y+row);row+=n;
    }
}
static void gateway_midi_overlay_blank(void)
{
    for(unsigned i=0;i<=NE+1;i++){
        unsigned x,y,w,h;gateway_midi_rect(i,&x,&y,&w,&h);
        for(unsigned row=0;row<h;){
            unsigned n=h-row;if(n>APP_CANVAS_PX/w)n=APP_CANVAS_PX/w;
            cv_begin(w,n,C_BLACK);cv_blit(x,y+row);row+=n;
        }
    }
}
