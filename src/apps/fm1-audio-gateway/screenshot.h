/* SPDX-License-Identifier: MPL-2.0
 * F0 7D 4C 49: begin 2; row 1,row(lo7,hi7),seq; end 0.
 * Control reply: 7F,op,status. Row reply: seq,row,chunk,30 RGB565 nibble pixels.
 * Freeze foreground visuals only; IRQ capture, audio and radio remain live. */
static uint8_t gateway_shot_pending,gateway_shot_chunk,gateway_shot_seq,gateway_shot_ready,gateway_shot_ack;
static uint16_t gateway_shot_row;
static uint32_t gateway_shot_time,gateway_shot_start,gateway_shot_session;
static void gateway_shot_redraw(void){
    gateway_page_redraw=gateway_redraw=gateway_meter_chrome_redraw=1;
    gateway_bar_key=~0u;gateway_mixer_redraw=1;
}
static void gateway_screenshot_cancel(void){
    gateway_shot_pending=gateway_shot_ack=0;
    if(gateway_shot_frozen){gateway_shot_frozen=0;gateway_shot_ready=0;gateway_shot_redraw();}
}
static void gateway_shot_control(unsigned op,unsigned status){
    uint8_t f[]={0xf0,0x7d,0x4c,0x49,0x7f,op,status,0xf7};
    if(plat_sysex_room()>=sizeof f)plat_sysex_send(f,sizeof f);
}
int app_sysex(const uint8_t *p,uint32_t n){
    if(n>=3 && p[0]==0x7d && p[1]==0x4c && p[2]==0x49){
        if(n==4 && p[3]==0){gateway_screenshot_cancel();gateway_shot_control(0,1);}
        else if(n==4 && p[3]==2){
            if(!gateway_ui_ready || !plat_usb_session()){gateway_shot_control(2,0);return 1;}
            if(!gateway_shot_frozen){
                gateway_shot_panel.cpu=plat_cpu_work_pct();gateway_shot_panel.cpu2=plat_cpu2_work_pct();
                gateway_shot_panel.raw=plat_battery_raw();gateway_shot_panel.master=plat_master();
                gateway_shot_panel.usb=plat_usb_connected();gateway_shot_panel.connected=plat_bt_receiver_connected();
                gateway_shot_panel.pairing=plat_bt_receiver_pairing();gateway_shot_panel.codec=plat_bt_receiver_codec();
                gateway_shot_start=plat_ms();gateway_shot_session=plat_usb_session();
                gateway_shot_frozen=1;gateway_shot_ready=0;
            }
            gateway_shot_time=plat_ms();gateway_shot_ack=1;
        }else if(n==7 && p[3]==1 && p[4]+128u*p[5]<240u && p[6]<128u){
            if(!gateway_shot_frozen || !gateway_shot_ready){gateway_shot_control(1,0);return 1;}
            gateway_shot_row=p[4]+128u*p[5];gateway_shot_seq=p[6];
            gateway_shot_chunk=0;gateway_shot_pending=1;gateway_shot_time=plat_ms();
        }
        return 1;
    }
#ifdef APP_UI_PROFILE
    return gateway_profile_sysex(p,n);
#else
    return 0;
#endif
}
static void gateway_shot_paint(void){
    gateway_screenshot_painting=1;gateway_bar_key=~0u;gateway_draw_cpu();
    if(gateway_screen==GATEWAY_SETTINGS)gateway_settings_draw();
    else if(gateway_screen==GATEWAY_FX){
        uint8_t dialog=gateway_fx_dialog;gateway_fx_dialog=0;gateway_fx_draw();
        gateway_fx_dialog=dialog;if(dialog)gateway_fx_draw();
    }else if(gateway_screen==GATEWAY_MIXER)gateway_mixer_draw(1);
    else{
        if(gateway_has_wave()){gateway_draw_wave(1);gateway_draw_zoom();}
        if(gateway_has_spectrum())gateway_spectrum_paint(1);
        if(gateway_has_meters())gateway_draw_meter(1);
    }
    gateway_screenshot_painting=0;
}
static void gateway_screenshot_step(void){
    if(!gateway_shot_frozen)return;
    if(!gateway_shot_session || gateway_shot_session!=plat_usb_session() ||
       plat_ms()-gateway_shot_time>5000u || plat_ms()-gateway_shot_start>30000u){gateway_screenshot_cancel();return;}
    if(!gateway_shot_ready){
        plat_cpu_work_begin();
        if(gateway_page_clear){lcd_fill(0,18,240,222,C_BLACK);gateway_page_clear=0;}
        gateway_shot_paint();plat_cpu_work_end();
        gateway_shot_ready=1;
    }
    if(gateway_shot_ack){gateway_shot_control(2,1);gateway_shot_ack=0;}
    if(!gateway_shot_pending)return;
    if(gateway_shot_pending==1){
        plat_cpu_work_begin();plat_screenshot_begin(gateway_shot_row);
        gateway_shot_paint();plat_screenshot_end();plat_cpu_work_end();gateway_shot_pending=2;
    }
    uint8_t f[129]={0xf0,0x7d,0x4c,0x49,gateway_shot_seq,
        gateway_shot_row&127u,gateway_shot_row>>7,gateway_shot_chunk};
    for(unsigned i=0;i<30;i++){
        uint16_t v=plat_screenshot_pixel(gateway_shot_chunk*30u+i);
        for(unsigned j=0;j<4;j++)f[8+i*4+j]=(v>>(12-4*j))&15u;
    }
    f[128]=0xf7;
    if(plat_sysex_room()>=sizeof f && !plat_sysex_send(f,sizeof f)){
        if(++gateway_shot_chunk==8)gateway_shot_pending=0;
    }
}
