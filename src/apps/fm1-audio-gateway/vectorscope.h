/* SPDX-License-Identifier: MPL-2.0
 * Foreground stereo vectorscope, rotated 45 degrees: mono is vertical,
 * opposite polarity horizontal, L-only leans left and R-only right.
 * The latest 2048 paired samples (~46 ms) supply persistence without FFTs or
 * an audio-callback consumer. Auto gain affects only the plotted points.
 * All scratch is view-owned. */
#define GATEWAY_VECTOR_SIZE 176u
#define GATEWAY_VECTOR_BYTES (GATEWAY_VECTOR_SIZE / 8u)
#define GATEWAY_VECTOR_X 32u
#define GATEWAY_VECTOR_Y 20u
#define GATEWAY_VECTOR_CENTER 88
#define GATEWAY_VECTOR_RADIUS 86
#define GATEWAY_VECTOR_UNITY (GATEWAY_VECTOR_RADIUS * 16u)
#define GATEWAY_VECTOR_INTERVAL 16u /* ~60 FPS, matching the waveform cadence. */
#define GATEWAY_VECTOR_FRAMES 2048u
typedef struct {
    int16_t pcm[GATEWAY_VECTOR_FRAMES][2];
    uint8_t plot[GATEWAY_VECTOR_SIZE][GATEWAY_VECTOR_BYTES];
    uint8_t drawn[GATEWAY_VECTOR_SIZE][GATEWAY_VECTOR_BYTES];
    uint32_t ms,position,scale,gain_ms;
    uint8_t have_position;
} gateway_vectorscope_buffers_t;
static gateway_vectorscope_buffers_t *gateway_vectorscope_buffers;
static uint16_t gateway_vectorscope_color_value(void);
static int gateway_vectorscope_auto_gain_enabled(void);

static void gateway_vectorscope_point(int16_t left,int16_t right,uint32_t multiplier,unsigned *x,unsigned *y)
{
    /* One reciprocal per capture, two multiplies per point. The linked scale
     * is >= both channel peaks, bounding each product by RADIUS * 2^20.
     * Signed division rounds symmetrically, including at either PCM rail. */
    *x=GATEWAY_VECTOR_CENTER+((int32_t)right-left)*(int32_t)multiplier/1048576;
    *y=GATEWAY_VECTOR_CENTER-((int32_t)left+right)*(int32_t)multiplier/1048576;
}
static void gateway_vectorscope_capture(uint32_t now)
{
    gateway_vectorscope_buffers_t *v=gateway_vectorscope_buffers;
    uint32_t position=0;
    int valid=plat_usb_input_spectrum_window(v->pcm,GATEWAY_VECTOR_FRAMES,&position);
    v->ms=now;
    if(valid && v->have_position && position==v->position)return;
    if(!valid && !v->have_position)return;
    memset(v->plot,0,sizeof v->plot);
    v->have_position=(uint8_t)valid;v->position=position;
    if(!valid){v->scale=0;return;} /* Drop stale traces and gain on stop/disconnect. */
    uint32_t multiplier=GATEWAY_VECTOR_UNITY;
    if(gateway_vectorscope_auto_gain_enabled()){
        uint32_t peak=32; /* Maximum gain ~60 dB; do not chase quantization noise. */
        for(unsigned i=0;i<GATEWAY_VECTOR_FRAMES;i++){
            if(!(i&255u))plat_bt_receiver_service();
            for(unsigned ch=0;ch<2;ch++){
                int32_t sample=v->pcm[i][ch];uint32_t level=sample<0?-sample:sample;
                if(level>peak)peak=level;
            }
        }
        uint32_t dt=now-v->gain_ms;if(dt>250u)dt=250u;
        /* Zoom out immediately for louder peaks, zoom in gently as they decay. */
        if(!v->scale || peak>=v->scale)v->scale=peak;
        else v->scale-=(v->scale-peak)*dt/(600u+dt);
        multiplier=((uint32_t)GATEWAY_VECTOR_RADIUS<<20)/(2u*v->scale);
    }else v->scale=0;
    v->gain_ms=now;
    for(unsigned i=0;i<GATEWAY_VECTOR_FRAMES;i++){
        if(!(i&255u))plat_bt_receiver_service();
        unsigned x,y;gateway_vectorscope_point(v->pcm[i][0],v->pcm[i][1],multiplier,&x,&y);
        v->plot[y][x>>3]|=(uint8_t)(1u<<(x&7u));
    }
}
static void gateway_vectorscope_paint(int force)
{
    gateway_vectorscope_buffers_t *v=gateway_vectorscope_buffers;
    uint16_t ink=swap16(gateway_vectorscope_color_value()),grid=swap16(RGB(42,42,46));
    if(force){
        cv_begin(240,12,C_BLACK);
        cv_text(22,0,&FONT_T,"L",C_GRAY);cv_text(212,0,&FONT_T,"R",C_GRAY);
        cv_blit(0,GATEWAY_VECTOR_Y);
    }
    /* At most eleven canvas transfers, and none for unchanged/silent plots.
     * Compare bitmap bytes, repaint only changed byte-aligned strips, and
     * restore the guides underneath erased points. Screenshot replay only
     * paints this frozen bitmap; it never consumes samples or ages a trace. */
    for(unsigned band=0;band<GATEWAY_VECTOR_SIZE;band+=16u){
        unsigned first=GATEWAY_VECTOR_BYTES,last=0;
        for(unsigned y=band;y<band+16u;y++)for(unsigned b=0;b<GATEWAY_VECTOR_BYTES;b++)
            if(force || v->plot[y][b]!=v->drawn[y][b]){
                if(b<first)first=b;
                if(b+1u>last)last=b+1u;
            }
        if(!last)continue;
        unsigned x0=first*8u,width=(last-first)*8u;
        cv_begin(width,16,C_BLACK);
        for(unsigned row=0;row<16u;row++)for(unsigned col=0;col<width;col++){
            unsigned x=x0+col,y=band+row;
            int dx=(int)x-GATEWAY_VECTOR_CENTER,dy=(int)y-GATEWAY_VECTOR_CENTER;
            unsigned ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
            if(ax+ay==GATEWAY_VECTOR_RADIUS || (!dx && ay<=GATEWAY_VECTOR_RADIUS) ||
               (!dy && ax<=GATEWAY_VECTOR_RADIUS) || (ax==ay && ax+ay<=GATEWAY_VECTOR_RADIUS))
                cv_px[row*width+col]=grid;
            if(v->plot[y][x>>3]&(1u<<(x&7u)))cv_px[row*width+col]=ink;
        }
        cv_blit(GATEWAY_VECTOR_X+x0,GATEWAY_VECTOR_Y+band);
    }
    memcpy(v->drawn,v->plot,sizeof v->drawn);
}
