/* SPDX-License-Identifier: MPL-2.0
 * Small original EQ response editor and limiter meters. No FFT or bitmap assets. */
static const uint16_t gateway_fx_colors[]={RGB(75,210,150),RGB(75,155,255),RGB(245,195,65),RGB(210,110,245),RGB(255,120,100)};
#define FX_GRAPH_X 22u
#define FX_GRAPH_W 214u
#define FX_GRAPH_TOP 34u
#define FX_GRAPH_BOTTOM 140u
static void gateway_fx_format(char text[40],const char *prefix,int value,unsigned decimals,const char *suffix)
{
    char digits[12];unsigned n=0,k=0;uint32_t v=value<0?(uint32_t)-value:(uint32_t)value;
    while(*prefix && n<20)text[n++]=*prefix++;
    if(value<0)text[n++]='-';
    do{digits[k++]='0'+v%10u;v/=10u;}while(v || k<=decimals);
    while(k){text[n++]=digits[--k];if(decimals && k==decimals)text[n++]='.';}
    while(*suffix && n<39)text[n++]=*suffix++;
    text[n]=0;
}
static void gateway_fx_center(int x,int y,int width,const char *text,uint16_t color)
{
    cv_text(x+(width-text_w(&FONT_T,text))/2,y,&FONT_T,text,color);
}
static void gateway_fx_value(unsigned y,const char *prefix,int value,unsigned decimals,const char *suffix)
{
    char text[40];gateway_fx_format(text,prefix,value,decimals,suffix);
    draw_text_box(6,y,228,&FONT_T,text,C_WHITE,0);
}
static unsigned gateway_fx_x(unsigned hz)
{
    float x=fm_log2f((float)hz/20.0f)*((float)(FX_GRAPH_W-1u)/9.965784285f);
    return FX_GRAPH_X+(unsigned)gateway_fx_clamp((int)(x+0.5f),0,FX_GRAPH_W-1);
}
static uint8_t gateway_fx_y(float db)
{
    return (uint8_t)gateway_fx_clamp((int)(87.0f-db*(106.0f/48.0f)+0.5f),FX_GRAPH_TOP,FX_GRAPH_BOTTOM);
}
/* Analog prototype evaluated at bilinear-warped frequency: exactly the same
 * pole pairs as fx_build, without cancellation around 20 Hz/high resonance. */
static float gateway_fx_power(const fx_shape_t *s,float warped)
{
    float u=warped/s->g,t=1-u*u,im=s->k*u;
    float re=s->m0*t+s->m2,ni=(s->m0*s->k+s->m1)*u;
    return (re*re+ni*ni)/(t*t+im*im);
}
static void gateway_fx_curves(const fx_params_t *p,uint8_t curves[6][FX_GRAPH_W])
{
    fx_shape_t shapes[FX_BANDS][FX_STAGES];unsigned counts[FX_BANDS];
    for(unsigned i=0;i<FX_BANDS;i++){
        counts[i]=fx_stages(p,i);
        for(unsigned j=0;j<counts[i];j++)shapes[i][j]=fx_shape(&p->band[i],fx_stage_q(&p->band[i],counts[i],j));
    }
    for(unsigned x=0;x<FX_GRAPH_W;x++){
        float hz=20.0f*fm_exp2f((float)x*(9.965784285f/(FX_GRAPH_W-1u)));
        float warped=fm_tanf(FM_PI*hz/FX_RATE),total=(float)p->post*0.1f;
        for(unsigned i=0;i<FX_BANDS;i++){
            float power=1;
            for(unsigned j=0;j<counts[i];j++)power*=gateway_fx_power(&shapes[i][j],warped);
            float db=power>1e-12f?3.010299956f*fm_log2f(power):-120.0f;
            curves[i][x]=gateway_fx_y(db);
            if(p->band[i].on)total+=db;
        }
        curves[5][x]=gateway_fx_y(p->eq?total:0);
    }
}
static void gateway_fx_graph(void)
{
    uint8_t curves[6][FX_GRAPH_W];gateway_fx_curves(&gateway_fx_params,curves);
    static const unsigned hz[]={20,100,1000,10000,20000};
    static const char *const labels[]={"20","100","1K","10K","20K"};
    for(unsigned band=34;band<155;band+=APP_CANVAS_PX/240u){
        unsigned h=155-band;if(h>APP_CANVAS_PX/240u)h=APP_CANVAS_PX/240u;
        cv_begin(240,h,C_BLACK);cv_oy=-(int)band;
        for(unsigned x=0;x<FX_GRAPH_W;x++){
            unsigned y=curves[5][x],lo=y<87?y:87,hi=y<87?87:y;
            cv_rect(FX_GRAPH_X+x,lo,1,hi-lo,RGB(22,24,13));
        }
        for(unsigned i=0;i<5;i++){
            unsigned x=gateway_fx_x(hz[i]);cv_line(x,FX_GRAPH_TOP,x,FX_GRAPH_BOTTOM,RGB(28,31,35));
            int left=(int)x-text_w(&FONT_T,labels[i])/2;if(left<20)left=20;
            if(i==3)left=(int)x-text_w(&FONT_T,labels[i]);
            if(left+text_w(&FONT_T,labels[i])>238)left=238-text_w(&FONT_T,labels[i]);
            cv_text(left,143,&FONT_T,labels[i],C_GRAY);
        }
        for(unsigned i=0;i<5;i++)cv_line(FX_GRAPH_X,34+i*106/4,235,34+i*106/4,i==2?RGB(65,70,74):RGB(28,31,35));
        cv_text(1,29,&FONT_T,"+24",C_GRAY);cv_text(9,81,&FONT_T,"0",C_GRAY);cv_text(1,134,&FONT_T,"-24",C_GRAY);
        for(unsigned i=0;i<6;i++){
            uint16_t color=(!gateway_fx_params.eq)?C_GRAY:i==5?RGB(240,220,115):gateway_fx_params.band[i].on?gateway_fx_colors[i]:RGB(65,65,65);
            for(unsigned x=1;x<FX_GRAPH_W;x++)cv_line(FX_GRAPH_X+x-1,curves[i][x-1],FX_GRAPH_X+x,curves[i][x],color);
        }
        for(unsigned i=0;i<FX_BANDS;i++){
            unsigned x=gateway_fx_x(gateway_fx_params.band[i].hz),y=curves[i][x-FX_GRAPH_X];
            uint16_t color=gateway_fx_params.eq && gateway_fx_params.band[i].on?gateway_fx_colors[i]:C_GRAY;
            unsigned size=gateway_fx_row==i+2?7:5;
            cv_rect((int)x-(int)size/2,(int)y-(int)size/2,size,size,color);
            cv_rect((int)x-1,(int)y-1,3,3,C_BLACK);
        }
        cv_oy=0;cv_blit(0,band);
    }
}
static void gateway_fx_icon(unsigned type,int x,int y,uint16_t color)
{
    static const int8_t curves[5][9]={
        {11,11,10,5,1,5,10,11,11}, {3,3,3,3,4,7,11,14,16},
        {16,14,11,7,4,3,3,3,3}, {3,3,4,6,9,11,12,12,12}, {12,12,12,11,9,6,4,3,3}};
    cv_line(x,y+11,x+24,y+11,RGB(45,49,54));
    for(unsigned i=1;i<9;i++)cv_line(x+3*(i-1),y+curves[type][i-1],x+3*i,y+curves[type][i],color);
}
static void gateway_fx_cards(void)
{
    for(unsigned i=0;i<FX_BANDS;i++){
        const fx_band_t *b=&gateway_fx_params.band[i];int selected=gateway_fx_row==i+2;
        uint16_t color=b->on?gateway_fx_colors[i]:RGB(90,90,90);char text[40];
        cv_begin(48,70,selected?RGB(24,33,43):RGB(10,13,17));
        cv_rect(1,0,46,selected?3:1,color);cv_rect(47,0,1,70,C_BLACK);
        gateway_fx_format(text,"",i+1,0,"");cv_text(3,4,&FONT_T,text,color);
        gateway_fx_icon(b->type,15,8,color);
        if(b->hz<1000)gateway_fx_format(text,"",b->hz,0,"Hz");
        else gateway_fx_format(text,"",(b->hz+5)/10,2,"k");
        gateway_fx_center(0,28,48,text,C_WHITE);
        if(b->type==FX_LOW_PASS || b->type==FX_HIGH_PASS)gateway_fx_format(text,"",12*(gateway_fx_params.slope[i]+1),0,"dB/oct");
        else gateway_fx_format(text,b->gain>0?"+":"",b->gain,1,"dB");
        gateway_fx_center(0,41,48,text,color);
        gateway_fx_format(text,b->type==FX_BELL?"Q":"R",b->q,2,"");gateway_fx_center(0,54,48,text,C_GRAY);
        cv_blit(i*48,170);
    }
}
static void gateway_fx_eq_draw(void)
{
    const fx_params_t *p=&gateway_fx_params;char text[40];
    cv_begin(240,16,C_BLACK);cv_text(5,1,&FONT_T,"FX: EQUALIZER",p->eq?C_WHITE:C_GRAY);
    gateway_fx_format(text,"Preset ",gateway_fx_selected[0]+1,0,"");
    cv_text(235-text_w(&FONT_T,text),1,&FONT_T,text,gateway_fx_preset_dirty(0)?RGB(255,212,40):C_WHITE);
    cv_blit(0,18);gateway_fx_graph();
    cv_begin(240,15,C_BLACK);
    const char *hint=gateway_fx_row==1?"K1 POST":gateway_fx_types[p->band[gateway_fx_row-2].type];
    cv_text(3,1,&FONT_T,hint,C_GRAY);
    gateway_fx_format(text,"post EQ gain: ",p->post,1,"dB");
    int x=237-text_w(&FONT_T,text);
    if(gateway_fx_row==1){cv_rect(x-3,0,text_w(&FONT_T,text)+6,15,RGB(24,46,65));cv_rect(x-3,0,2,15,RGB(75,210,150));}
    cv_text(x,1,&FONT_T,text,C_WHITE);cv_blit(0,155);
    gateway_fx_cards();
}
static uint8_t gateway_fx_status_valid;
static uint32_t gateway_fx_status_drawn[3];
static void gateway_fx_view(void)
{
    unsigned enable=gateway_screen==GATEWAY_FX && gateway_fx_limiter;
    uint32_t lock=plat_lock();gateway_fx.meter_enabled=enable;
    gateway_fx.output_peak[0]=gateway_fx.output_peak[1]=0;gateway_fx.meter_reduction=65536u;plat_unlock(lock);
    memset(gateway_fx_meter,0,sizeof gateway_fx_meter);gateway_fx_reduction=0;gateway_fx_status_valid=0;
    gateway_fx_meter_ms=plat_ms();gateway_fx_draw_ms=plat_ms()-48u;
}
static void gateway_fx_limiter_region(unsigned left,unsigned top,unsigned width,unsigned height,int full)
{
    unsigned active=gateway_fx_params.lim;
    for(unsigned band=top;band<top+height;band+=APP_CANVAS_PX/width){
        unsigned h=top+height-band;if(h>APP_CANVAS_PX/width)h=APP_CANVAS_PX/width;
        cv_begin(width,h,C_BLACK);cv_oy=-(int)band;
        static const char *const ticks[]={"0","-12","-24","-36","-48","-60"};
        if(full)for(unsigned i=0;i<6;i++)cv_text(156-text_w(&FONT_T,ticks[i]),44+36*i,&FONT_T,ticks[i],C_GRAY);
        if(full || left)for(unsigned ch=0;ch<2;ch++){
            const gateway_meter_t *m=&gateway_fx_meter[ch];int x=163+27*ch-(int)left;
            for(unsigned i=0;i<60;i++)cv_rect(x,227-3*i,22,2,m->level>=(i+1)*10?
                (!active?RGB(90,90,90):i>=54?RGB(255,60,45):i>=42?RGB(235,185,45):RGB(40,195,100)):RGB(22,27,29));
            if(m->held)cv_rect(x,230-m->held*179/600,22,1,active?C_WHITE:C_GRAY);
        }
        if(full || left)cv_rect(224-(int)left,50,5,180,RGB(45,20,22));
        unsigned gr_height=gateway_fx_reduction*180/240;if(gr_height>180)gr_height=180;
        if(full || left)cv_rect(224-(int)left,50,5,gr_height,active?RGB(255,55,60):RGB(75,60,60));
        char text[40];
        if(!left){gateway_fx_format(text,"GR ",gateway_fx_reduction,1,"dB");cv_text(6,204,&FONT_T,text,active?RGB(255,80,80):C_GRAY);}
        if(full || left){gateway_fx_format(text,"",(int)(gateway_fx_meter[0].held>gateway_fx_meter[1].held?gateway_fx_meter[0].held:gateway_fx_meter[1].held)-600,1,"dB");
        gateway_fx_center(158-(int)left,35,58,text,active?C_WHITE:C_GRAY);}
        if(full){cv_text(6,48,&FONT_T,"K1 INPUT",C_GRAY);gateway_fx_format(text,"",gateway_fx_params.input,1,"dB");cv_text(6,64,&FONT_XS,text,C_WHITE);
        cv_text(6,99,&FONT_T,"K2 RELEASE",C_GRAY);gateway_fx_format(text,"",gateway_fx_params.release,0,"ms");cv_text(6,115,&FONT_XS,text,C_WHITE);
        cv_text(6,150,&FONT_T,"K3 CEILING",C_GRAY);gateway_fx_format(text,"",gateway_fx_params.ceiling,1,"dB");cv_text(6,166,&FONT_XS,text,C_WHITE);}
        cv_oy=0;cv_blit(left,band);
    }
}
static void gateway_fx_status(void)
{
    if(!gateway_fx_limiter)return;
    uint32_t now=plat_ms(),dt=now-gateway_fx_meter_ms,peak[2],reduction,lock=plat_lock();
    peak[0]=gateway_fx.output_peak[0];peak[1]=gateway_fx.output_peak[1];reduction=gateway_fx.meter_reduction;
    gateway_fx.output_peak[0]=gateway_fx.output_peak[1]=0;gateway_fx.meter_reduction=65536u;plat_unlock(lock);
    if(dt>1000)dt=1000;
    gateway_fx_meter_ms=now;gateway_fx_draw_ms=now;
    for(unsigned i=0;i<2;i++)gateway_meter_step(&gateway_fx_meter[i],peak[i],dt);
    unsigned gr=0;
    if(reduction<65536u){float g=(float)reduction/65536.0f;gr=g>0?(unsigned)(-60.2059991f*fm_log2f(g)+0.5f):1200;}
    unsigned fall=dt*240u/1000u;gateway_fx_reduction=gateway_fx_reduction>fall?gateway_fx_reduction-fall:0;
    if(gr>gateway_fx_reduction)gateway_fx_reduction=gr;
    uint32_t max_held=gateway_fx_meter[0].held>gateway_fx_meter[1].held?gateway_fx_meter[0].held:gateway_fx_meter[1].held;
    uint32_t key[3]={0,max_held,gateway_fx_reduction};
    for(unsigned i=0;i<2;i++) {
        const gateway_meter_t *m=&gateway_fx_meter[i];
        unsigned held=m->held?230u-m->held*179u/600u:0;
        key[0]|=(m->level/10u)<<(6u*i);
        key[0]|=held<<(12u+8u*i);
    }
    if(!gateway_fx_status_valid)gateway_fx_limiter_region(0,34,240,206,1);
    else {
        if(key[0]!=gateway_fx_status_drawn[0] || key[1]!=gateway_fx_status_drawn[1] || key[2]!=gateway_fx_status_drawn[2])
            gateway_fx_limiter_region(158,34,76,206,0);
        if(key[2]!=gateway_fx_status_drawn[2])gateway_fx_limiter_region(0,204,134,16,0);
    }
    memcpy(gateway_fx_status_drawn,key,sizeof key);gateway_fx_status_valid=1;
}

static void gateway_fx_draw(void)
{
    if(gateway_fx_dialog){
        char text[40];gateway_fx_format(text,gateway_fx_dialog==2?"Initialize preset ":"Save preset ",gateway_fx_selected[gateway_fx_limiter]+1,0,"?");
        for(unsigned band=0;band<96;band+=APP_CANVAS_PX/216u){
            unsigned h=96-band;if(h>APP_CANVAS_PX/216u)h=APP_CANVAS_PX/216u;
            cv_begin(216,h,RGB(18,24,31));cv_oy=-(int)band;
            cv_rect(0,0,216,1,C_GRAY);cv_rect(0,95,216,1,C_GRAY);cv_rect(0,0,1,96,C_GRAY);cv_rect(215,0,1,96,C_GRAY);
            gateway_fx_center(0,20,216,text,C_WHITE);
            gateway_fx_center(0,47,108,"CANCEL",C_WHITE);
            gateway_fx_center(108,47,108,gateway_fx_dialog==2?"YES":"SAVE",C_WHITE);
            gateway_fx_center(0,63,108,"OCT-",C_GRAY);
            gateway_fx_center(108,63,108,"OCT+",C_GRAY);
            if(gateway_fx_notice==2)gateway_fx_center(0,81,216,"SAVE FAILED",RGB(255,48,48));
            cv_oy=0;cv_blit(12,80+band);
        }
        return;
    }
    if(!gateway_fx_limiter){gateway_fx_eq_draw();return;}
    cv_begin(240,16,C_BLACK);cv_text(5,1,&FONT_T,"FX: PEAK LIMITER",gateway_fx_params.lim?C_WHITE:C_GRAY);
    char text[40];gateway_fx_format(text,"Preset ",gateway_fx_selected[1]+1,0,"");
    cv_text(235-text_w(&FONT_T,text),1,&FONT_T,text,gateway_fx_preset_dirty(1)?RGB(255,212,40):C_WHITE);
    cv_blit(0,18);gateway_fx_status_valid=0;
    if(gateway_screenshot_painting)gateway_fx_limiter_region(0,34,240,206,1);else gateway_fx_status();
}
