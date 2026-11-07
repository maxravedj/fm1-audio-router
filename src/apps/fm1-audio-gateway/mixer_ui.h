/* SPDX-License-Identifier: MPL-2.0
 * Mixer inputs after applied faders, before summing/FX; MASTER after FX/pot. */
static uint32_t gateway_mixer_bar_key[8],gateway_mixer_peak_key[4],gateway_mixer_fader_key[4];
static uint8_t gateway_mixer_switch_key[4];
static void gateway_mixer_switch(unsigned x,unsigned y,const char *label,int on,uint16_t color){
    cv_begin(14,15,color);
    if(!on)cv_rect(1,1,12,13,C_BLACK);
    cv_text((14-text_w(&FONT_T,label))/2,1,&FONT_T,label,on?C_BLACK:color);cv_blit(x,y);
}
static void gateway_mixer_draw(int force)
{
    uint32_t now=plat_ms(),dt=now-gateway_meter_ms;
    if(!gateway_screenshot_painting && (force || dt>=GATEWAY_METER_INTERVAL)){
        uint32_t peak[3][2];if(dt>60000)dt=60000;gateway_meter_ms=now;
        uint32_t lock=plat_lock();
        for(unsigned s=0;s<3;s++)for(unsigned ch=0;ch<2;ch++){
            peak[s][ch]=gateway_mix_peak[s][ch];gateway_mix_peak[s][ch]=0;
        }
        plat_unlock(lock);
        for(unsigned ch=0;ch<2;ch++){
            gateway_meter_step(&gateway_meter[ch],peak[0][ch],dt);
            gateway_meter_step(&gateway_meter[4+ch],peak[1][ch],dt);
        }
        for(unsigned ch=0;ch<2;ch++)gateway_meter_step(&gateway_meter[6+ch],peak[2][ch],dt);
        plat_master_peaks(peak[0]);for(unsigned ch=0;ch<2;ch++)gateway_meter_step(&gateway_meter[2+ch],peak[0][ch],dt);
    }
    if(force){
        draw_text_box(0,21,240,&FONT_T,"MIXER",C_WHITE,1);
        /* One shared scale, aligned with the existing -60..0 dB bars/peaks. */
        static const char *ticks[]={"0","-6","-12","-24","-36","-48","-60"};
        static const unsigned levels[]={600,540,480,360,240,120,0};
        cv_begin(20,158,C_BLACK);
        for(unsigned t=0;t<7;t++)cv_text(18-text_w(&FONT_T,ticks[t]),145-levels[t]*145/600,&FONT_T,ticks[t],C_GRAY);
        cv_blit(0,58);
    }
    uint32_t master=gateway_shot_frozen?gateway_shot_panel.master:plat_master();if(master>4096)master=4096;
    for(unsigned g=0;g<4;g++){
        unsigned base=20+55*g,pair=g==0?0:g==1?4:g==2?6:2;
        int muted=gateway_mixer_cut(g);
        if(force)draw_text_box(base,220,34,&FONT_T,gateway_sources[gateway_source_order[g]],C_WHITE,1);
        unsigned switches=gateway_mixer_muted(g) | ((gateway_mix_solos>>g)&1u)<<1;
        if(force || switches!=gateway_mixer_switch_key[g]){
            gateway_mixer_switch(base+37,180,"M",switches&1u,RGB(255,48,48));
            gateway_mixer_switch(base+37,197,"S",switches&2u,RGB(255,212,40));
            gateway_mixer_switch_key[g]=switches;
        }
        unsigned held=gateway_meter[pair].held>gateway_meter[pair+1].held?gateway_meter[pair].held:gateway_meter[pair+1].held;
        uint32_t peak_key=held|((unsigned)muted<<16);
        int db=g<3?gateway_mix_db[g]:gateway_master_db(master);
        uint32_t fader_key=g<3?(unsigned)(db-GATEWAY_GAIN_OFF):master;
        if(force || peak_key!=gateway_mixer_peak_key[g] || fader_key!=gateway_mixer_fader_key[g]){
            char text[40];cv_begin(55,16,C_BLACK);
            gateway_peak_label(text,held);cv_text(17-text_w(&FONT_T,text)/2,0,&FONT_T,text,muted?RGB(160,160,160):C_WHITE);
            if(g<3?db<=GATEWAY_GAIN_OFF:!master)memcpy(text,"-INF",5);
            else gateway_fx_format(text,"",db<=GATEWAY_GAIN_MIN?(db-5)/10:db,db<=GATEWAY_GAIN_MIN?0:1,"");
            cv_text(44-text_w(&FONT_T,text)/2,0,&FONT_T,text,g<3?RGB(80,170,250):C_WHITE);
            cv_blit(base,40);gateway_mixer_peak_key[g]=peak_key;
        }
        for(unsigned ch=0;ch<2;ch++){
            const gateway_meter_t *m=&gateway_meter[pair+ch];unsigned index=2*g+ch;
            uint32_t key=m->level/20u | m->held<<6 | ((unsigned)muted<<17) | (m->clip_ms!=0)<<18;
            if(force || key!=gateway_mixer_bar_key[index]){
                cv_begin(16,152,C_BLACK);
                cv_rect(0,0,16,2,muted?RGB(45,45,45):m->clip_ms?RGB(255,48,48):RGB(45,18,18));
                for(unsigned i=0;i<30;i++)cv_rect(0,148-5*i,16,4,m->level>=(i+1)*20?
                    (muted?RGB(90,90,90):gateway_meter_color((i+1)*20)):(muted?RGB(20,20,20):RGB(21,26,26)));
                if(m->held)cv_rect(0,149-m->held*145/600,16,1,muted?RGB(160,160,160):C_WHITE);
                cv_blit(base+18*ch,60);gateway_mixer_bar_key[index]=key;
            }
        }
        if(force || fader_key!=gateway_mixer_fader_key[g]){
            cv_begin(10,116,C_BLACK);cv_rect(4,0,2,116,RGB(65,65,65));
            unsigned position=g<3?(unsigned)(db-GATEWAY_GAIN_OFF)*111u/(unsigned)-GATEWAY_GAIN_OFF:master*111u/4096u;
            cv_rect(0,111-position,10,5,g<3?RGB(80,170,250):C_WHITE);cv_blit(base+39,60);
            gateway_mixer_fader_key[g]=fader_key;
        }
    }
    gateway_mixer_redraw=0;
}
