/* SPDX-License-Identifier: MPL-2.0
 * Temporary RAM-only diagnostics, F0 7D 4C 50 op ... F7. All commands run in
 * foreground. No settings changes, device flashing or runtime core stop. */
#ifdef APP_UI_PROFILE
static uint8_t gateway_profile_mode,gateway_profile_source,gateway_profile_zoom,gateway_profile_auto;
static void gateway_profile_restore(void)
{
    if(plat_profile_info(2)&PROFILE_NO_WAVE_CAPTURE)plat_audio_monitor_reset(gateway_monitor);
    gateway_redraw=gateway_page_redraw=gateway_meter_chrome_redraw=1;gateway_bar_key=~0u;
}
static char *gateway_profile_num(char *p,uint32_t n)
{
    char s[10];unsigned i=0;do{s[i++]=(char)('0'+n%10u);n/=10u;}while(n);
    while(i)*p++=s[--i];*p=0;return p;
}
static char *gateway_profile_cat(char *p,const char *s){while(*s)*p++=*s++;*p=0;return p;}
static void gateway_profile_send(const char *s)
{
    uint8_t frame[128]={0xf0,0x7d,0x4c,0x50};unsigned n=4;
    while(*s && n<sizeof frame-1)frame[n++]=(uint8_t)*s++&0x7f;
    frame[n++]=0xf7;plat_sysex_send(frame,n);
}
static void gateway_profile_report(void)
{
    static const char *const names[]={"work","audio","usb_capture","master_capture","out_capture","bt_mix",
        "wave_read","wave_draw","spectrum_read","fft","spectrum_draw","lcd_wait","irq_lock"};
    char s[112],*p=gateway_profile_cat(s,"version " APP_VERSION " ms ");
    p=gateway_profile_num(p,plat_profile_info(1)/(1000u*PLAT_TICKS_PER_US));
    p=gateway_profile_cat(p," flags ");gateway_profile_num(p,plat_profile_info(2));gateway_profile_send(s);
    p=gateway_profile_cat(s,"view ");p=gateway_profile_num(p,gateway_profile_mode+1);
    p=gateway_profile_cat(p," source ");p=gateway_profile_cat(p,gateway_sources[gateway_profile_source]);
    p=gateway_profile_cat(p," window_ms ");p=gateway_profile_num(p,gateway_times[gateway_profile_zoom]);
    p=gateway_profile_cat(p," auto ");gateway_profile_num(p,gateway_profile_auto);gateway_profile_send(s);
    for(unsigned i=0;i<PROFILE_ROWS;i++){
        plat_profile_row_t r=plat_profile_row(i);p=gateway_profile_cat(s,names[i]);
        p=gateway_profile_cat(p," calls ");p=gateway_profile_num(p,r.calls);
        p=gateway_profile_cat(p," us ");p=gateway_profile_num(p,r.ticks/PLAT_TICKS_PER_US);
        p=gateway_profile_cat(p," max_us ");gateway_profile_num(p,r.max_ticks/PLAT_TICKS_PER_US);gateway_profile_send(s);
    }
    p=gateway_profile_cat(s,"lcd windows ");p=gateway_profile_num(p,plat_profile_counter(PROFILE_WINDOWS));
    p=gateway_profile_cat(p," bytes ");p=gateway_profile_num(p,plat_profile_counter(PROFILE_BYTES));
    p=gateway_profile_cat(p," dma ");gateway_profile_num(p,plat_profile_counter(PROFILE_DMA));gateway_profile_send(s);
    p=gateway_profile_cat(s,"cpu2_us ");p=gateway_profile_num(p,plat_profile_info(4)/PLAT_TICKS_PER_US);
    p=gateway_profile_cat(p," audio_xruns ");gateway_profile_num(p,plat_profile_info(3));gateway_profile_send(s);
    gateway_profile_send("end");
}
int gateway_profile_sysex(const uint8_t *data,uint32_t n)
{
    if(n<3 || data[0]!=0x7d || data[1]!=0x4c || data[2]!=0x50)return 0;
    if(n<4){gateway_profile_send("error command");return 1;}
    if(data[3]==1){
        if(n!=6 || gateway_screen!=GATEWAY_MAIN ||
           ((data[5]&(PROFILE_NO_WAVE_UI|PROFILE_NO_WAVE_CAPTURE)) && !gateway_has_wave()) ||
           !plat_profile_start(data[4],data[5])){gateway_profile_send("error start");return 1;}
        gateway_profile_mode=gateway_mode;gateway_profile_source=gateway_monitor;
        gateway_profile_zoom=(uint8_t)gateway_zoom;gateway_profile_auto=gateway_master_wave_auto_scale;
        gateway_profile_send("started");
    }else if(data[3]==0 || data[3]==2){
        if(plat_profile_stop())gateway_profile_restore();
        gateway_profile_report();
    }else gateway_profile_send("error command");
    return 1;
}
#endif
