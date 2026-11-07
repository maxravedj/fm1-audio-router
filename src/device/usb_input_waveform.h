/* SPDX-License-Identifier: MPL-2.0
 * Read-only stereo history. One USB ISR producer; main-loop readers use short
 * per-column locks, never a framebuffer-sized interrupt-disabled copy.
 * Seven 4:1 resolutions retain both extrema, including one-sample transients.
 * No allocation, division, drawing or floating point in the producer. */
#ifndef FM1B_USB_INPUT_WAVEFORM_H
#define FM1B_USB_INPUT_WAVEFORM_H
#define USB_WAVE_LEVELS 7u
#define USB_WAVE_RING 1024u
typedef union { int16_t v[4]; uint32_t words[2]; } usb_wave_bin_t; /* L min/max, R min/max */
#ifdef APP_DYNAMIC_DISPLAY
static volatile usb_wave_bin_t (*usb_wave_history)[USB_WAVE_RING];
#else
static volatile usb_wave_bin_t usb_wave_history[USB_WAVE_LEVELS][USB_WAVE_RING];
#endif
static usb_wave_bin_t usb_wave_accum[USB_WAVE_LEVELS];
static volatile uint32_t usb_wave_count, usb_wave_filled;
#ifdef APP_DYNAMIC_DISPLAY
static volatile uint8_t usb_input_wave_enabled;
#else
static volatile uint8_t usb_input_wave_enabled = 1;
#endif
static uint32_t usb_wave_packet_ms;
static uint8_t usb_wave_packet_seen;

/* Invalidate old bins in O(1), retaining the monotonic frame count. Partially
 * completed bins straddling the restart remain invalid (age > filled).
 * Do not clear the large history or hold interrupts off for a bulk copy. */
static void usb_input_wave_stream_start(void)
{
    usb_wave_filled = 0;
    usb_wave_packet_seen = 0;
}

uint32_t plat_usb_wave_mem_bytes(void)
{
#ifdef APP_DYNAMIC_DISPLAY
    return USB_WAVE_LEVELS * USB_WAVE_RING * sizeof(usb_wave_bin_t);
#else
    return 0;
#endif
}
void plat_usb_wave_mem(void *p)
{
#ifdef APP_DYNAMIC_DISPLAY
    uint32_t lock = plat_lock();
    usb_input_wave_enabled = 0;
    usb_wave_history = p;
    usb_input_wave_stream_start();
    plat_unlock(lock);
#else
    (void)p;
#endif
}

static void usb_input_wave_packet_begin(void)
{
    uint32_t now = plat_ms();
    /* Also handle suspended hosts / packet gaps without a SET_INTERFACE. */
    if (usb_wave_packet_seen && now - usb_wave_packet_ms >= 100u) usb_wave_filled = 0;
    usb_wave_packet_ms = now;
    usb_wave_packet_seen = 1;
}

static void usb_input_wave_push(int32_t l, int32_t r)
{
#ifdef APP_UI_PROFILE
    if(ui_profile_flags&PROFILE_NO_WAVE_CAPTURE)return;
#endif
    uint32_t level, count = usb_wave_count + 1u;
    usb_wave_bin_t bin = {{(int16_t)(l >> 8), (int16_t)(l >> 8),
                           (int16_t)(r >> 8), (int16_t)(r >> 8)}};
    volatile usb_wave_bin_t *dst=&usb_wave_history[0][(count-1u)&(USB_WAVE_RING-1u)];
    /* Aligned word accesses halve history-store transactions. Explicit extrema
     * avoid an inner half-word loop and per-sample stack shuffling at -Oz. */
    dst->words[0]=bin.words[0];dst->words[1]=bin.words[1];
    for (level = 1; level < USB_WAVE_LEVELS; level++) {
        uint32_t shift = 2u * level, child_shift = shift - 2u;
        usb_wave_bin_t *a = &usb_wave_accum[level];
        if ((((count - 1u) >> child_shift) & 3u) == 0) *a = bin;
        else {
            if(bin.v[0]<a->v[0])a->v[0]=bin.v[0];
            if(bin.v[1]>a->v[1])a->v[1]=bin.v[1];
            if(bin.v[2]<a->v[2])a->v[2]=bin.v[2];
            if(bin.v[3]>a->v[3])a->v[3]=bin.v[3];
        }
        if (count & ((1u << shift) - 1u)) break;
        bin = *a;
        dst=&usb_wave_history[level][((count>>shift)-1u)&(USB_WAVE_RING-1u)];
        dst->words[0]=bin.words[0];dst->words[1]=bin.words[1];
    }
    if (usb_wave_filled < USB_WAVE_RING * 4096u) usb_wave_filled++;
    usb_wave_count = count;
}

uint32_t plat_usb_input_wave_position(void)
{
    uint32_t lock = plat_lock(), n = usb_wave_count;
    plat_unlock(lock);
    return n;
}

/* Called under a short lock. A bin is valid only after it has completed and
 * before it has wrapped out of this resolution's ring. Unsigned sample ages
 * remain valid across the 32-bit frame counter wrap (~27 hours). */
static int usb_wave_read(uint32_t frame, uint32_t level, int16_t v[4])
{
    uint32_t shift = 2u * level, stride = 1u << shift;
    uint32_t age = usb_wave_count - frame, k;
    if (!usb_input_wave_enabled) return 0;
    if (age < stride || age > usb_wave_filled || age > USB_WAVE_RING * stride) return 0;
    for (k = 0; k < 4u; k++) v[k] = usb_wave_history[level][(frame >> shift) & (USB_WAVE_RING - 1u)].v[k];
    return 1;
}

void plat_usb_input_waveform(int16_t (*out)[4], uint32_t width,
                            uint32_t span, uint32_t end)
{
    uint32_t level = 0, stride, x, begin, step, remainder, fraction=0;
    if (!width || !span || span > 352800u || width > 240u) return;
    while (level + 1u < USB_WAVE_LEVELS && (1u << (2u * (level + 1u))) <= span / width) level++;
    stride = 1u << (2u * level);
    end &= ~(stride - 1u); /* completed bins only; scrolling tracks the sample clock */
    begin = end - span;
    /* Exact floor(x * span / width) partition with two divisions per frame,
     * rather than two per column. Keep the unrounded boundary for narrow
     * windows where several columns select the same source frame. */
    step=span/width;remainder=span%width;
    for (x = 0; x < width; x++) {
        uint32_t first=begin,last=begin+step;
        fraction+=remainder;
        if(fraction>=width){last++;fraction-=width;}
        begin=last;
        uint32_t frame, k, lock;
        int have = 0;
        if (last == first) last++;
        first &= ~(stride - 1u);
        lock = plat_lock();
        for (frame = first; (int32_t)(frame - last) < 0; frame += stride) {
            int16_t v[4] = {0, 0, 0, 0};
            usb_wave_read(frame, level, v);
            for (k = 0; k < 4u; k += 2u) {
                if (!have || v[k] < out[x][k]) out[x][k] = v[k];
                if (!have || v[k+1u] > out[x][k+1u]) out[x][k+1u] = v[k+1u];
            }
            have = 1;
        }
        plat_unlock(lock);
    }
}
#endif
