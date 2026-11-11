/* SPDX-License-Identifier: MPL-2.0 */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
int main(void){
 palette_set(APP_PALETTE);app_init();app_ui_init();gateway_cpu_mode=gateway_battery_mode=0;btr_host_on=1;
 for(unsigned state=0;state<384;state++){
  unsigned v=state;gateway_bt_mode=v&1;v>>=1;btr_host_connected=v&1;v>>=1;
  btr_host_pairing=v&1;v>>=1;btr_host_codec=v%6;v/=6;
  gateway_fx_params.chain=v&1;v>>=1;gateway_fx_params.eq=gateway_fx_params.lim=v&1;v>>=1;
  gateway_usb_out_mode=v&1;
  const char *bt=btr_host_connected?(btr_host_codec==PLAT_BT_CODEC_LDAC?"BT LDAC":btr_host_codec==PLAT_BT_CODEC_AAC?"BT AAC":btr_host_codec==PLAT_BT_CODEC_SBC?"BT SBC":btr_host_codec==PLAT_BT_CODEC_APTX?"BT aptX":btr_host_codec==PLAT_BT_CODEC_APTX_HD?"BT aptXHD":"BT"):"BT";
  const char *names[]={"USB IN",bt,"FX","USB OUT"};unsigned x=3;
  for(unsigned plugged=0;plugged<3;plugged++){
   /* Connected -> unplugged -> reconnected must invalidate cached colors. */
   hsx_session=plugged==1?0:1;gateway_draw_cpu();x=3;
   for(unsigned b=0;b<4;b++){
    unsigned w=text_w(&FONT_T,names[b])+3;
    /* Every outlined/filled badge has identical y2..15 vertical edges. */
    for(unsigned y=2;y<16;y++)assert(fb[y*240+x]!=0 && fb[y*240+x+w-1]!=0);
    if(b==0 || b==3){
     uint16_t expected=swap16(b==3 && !gateway_usb_out_mode?C_GRAY:hsx_session?RGB(20,150,50):RGB(190,25,25));
     assert(fb[2*240+x]==expected);
     if(b==3)assert(fb[3*240+x+1]==(gateway_usb_out_mode?expected:C_BLACK));
    }
    for(unsigned xx=x;xx<x+w;xx++)assert(!fb[xx] && !fb[240+xx]);
    if(b<3)for(unsigned y=0;y<16;y++)for(unsigned xx=x+w;xx<x+w+2;xx++)assert(!fb[y*240+xx]);
    x+=w+2;
   }
  }
 }
 assert(!cv_cuts);
 puts("PASS: all badge heights exactly 14px and gaps exactly 2px across 384 status combinations including LDAC; USB OUT gray/off, green/connected, red/disconnected with cached unplug/reconnect updates");
}
