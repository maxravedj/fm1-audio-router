/* SPDX-License-Identifier: MPL-2.0
 * Actual app record and actual A/B storage, with a NOR/power-cut model. */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
static uint8_t nor[0x100000];
static int cut=-1;
static int st_read(uint32_t off,void *dst,uint32_t n){assert(off>=0xD8000 && off+n<=0xDA000);memcpy(dst,nor+off,n);return 0;}
static int st_erase(uint32_t off){assert(off==0xD8000 || off==0xD9000);memset(nor+off,255,4096);return 0;}
static int st_prog(uint32_t off,const void *src,uint32_t n){
 assert(off>=0xD8000 && off+n<=0xDA000 && (off&255)+n<=256);
 if(!cut)return -9;if(cut>0)cut--;
 const uint8_t *p=src;for(unsigned i=0;i<n;i++)nor[off+i]&=p[i];return 0;
}
#include "../../../device/storage.c"
int main(void){
 memset(nor,255,sizeof nor);palette_set(APP_PALETTE);app_init();
 assert(sizeof st_buf==256 && sizeof(gateway_fx_presets_t)==456);
 gateway_fx_presets_t first=gateway_fx_presets_saved,next=first,loaded;
 next.eq[2].band[0].hz=1234;next.lim[5].ceiling=-68;next.selected[0]=2;next.selected[1]=5;
 assert(!st_save(OBJ_FX,&first,sizeof first));
 for(int i=0;i<3;i++){
  cut=i;assert(st_save(OBJ_FX,&next,sizeof next));cut=-1;
  assert(st_load(OBJ_FX,&loaded,sizeof loaded)==456 && !memcmp(&loaded,&first,456));
 }
 assert(!st_save(OBJ_FX,&next,sizeof next));
 assert(st_load(OBJ_FX,&loaded,sizeof loaded)==456 && !memcmp(&loaded,&next,456));
 nor[0xD9000+256+100]^=1; /* Corrupted latest payload falls back to A. */
 assert(st_load(OBJ_FX,&loaded,sizeof loaded)==456 && !memcmp(&loaded,&first,456));
 for(unsigned i=0;i<sizeof nor;i++)if(i<0xD8000 || i>=0xDA000)assert(nor[i]==255);
 puts("PASS: actual 456-byte AFX v4 bank over two flash pages, cuts before every page/commit retain old bank, CRC fallback, unchanged FX-only sectors and 256-byte bounce buffer");
}
