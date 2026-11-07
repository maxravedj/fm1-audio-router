/* SPDX-License-Identifier: MPL-2.0
 * On-demand scanline capture shared by the target LCD and host simulator. */
static uint16_t screenshot_pixels[240];
static uint32_t screenshot_y;
static uint8_t screenshot_active;
void plat_screenshot_begin(uint32_t row){
    screenshot_y=row;screenshot_active=1;
    for(unsigned x=0;x<240;x++)screenshot_pixels[x]=0;
}
void plat_screenshot_end(void){screenshot_active=0;}
uint16_t plat_screenshot_pixel(uint32_t x){return x<240?screenshot_pixels[x]:0;}
static void screenshot_copy(uint32_t x,uint32_t y,uint32_t w,uint32_t h,const uint16_t *px,uint16_t fill){
    if(screenshot_y<y || screenshot_y-y>=h || x>=240)return;
    for(unsigned i=0;i<w && x+i<240;i++){
        uint16_t v=px?px[(screenshot_y-y)*w+i]:0;
        screenshot_pixels[x+i]=px?(uint16_t)((v>>8)|(v<<8)):fill;
    }
}
