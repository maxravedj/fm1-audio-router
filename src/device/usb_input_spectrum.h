/* SPDX-License-Identifier: MPL-2.0
 * Raw stereo capture for foreground spectral analysis. The USB observer only
 * copies PCM; no FFT, windowing or floating point runs in an interrupt. */
#pragma once
#define USB_SPECTRUM_RING (2u * PLAT_SPECTRUM_FRAMES)
#ifdef APP_DYNAMIC_DISPLAY
static volatile int16_t (*usb_spectrum_ring)[2];
#else
static volatile int16_t usb_spectrum_ring[USB_SPECTRUM_RING][2];
#endif
static volatile uint32_t usb_spectrum_count, usb_spectrum_filled, usb_spectrum_generation;
static volatile uint8_t usb_spectrum_enabled;
static uint32_t usb_spectrum_packet_ms;
static uint8_t usb_spectrum_packet_seen;

static void usb_input_spectrum_stream_start(void)
{
    usb_spectrum_filled = 0;
    usb_spectrum_packet_seen = 0;
    usb_spectrum_generation++;
}

uint32_t plat_usb_spectrum_mem_bytes(void)
{
#ifdef APP_DYNAMIC_DISPLAY
    return USB_SPECTRUM_RING * 2u * sizeof(int16_t);
#else
    return 0;
#endif
}
void plat_usb_spectrum_mem(void *p)
{
#ifdef APP_DYNAMIC_DISPLAY
    uint32_t lock = plat_lock();
    usb_spectrum_enabled = 0;
    usb_spectrum_ring = p;
    usb_input_spectrum_stream_start();
    plat_unlock(lock);
#else
    (void)p;
#endif
}

void plat_usb_spectrum_enable(int enabled)
{
    uint32_t lock = plat_lock();
    usb_input_spectrum_stream_start();
    usb_spectrum_enabled = enabled != 0;
#ifdef APP_DYNAMIC_DISPLAY
    if (!usb_spectrum_ring) usb_spectrum_enabled = 0;
#endif
    plat_unlock(lock);
}

static void usb_input_spectrum_packet_begin(void)
{
    uint32_t now = plat_ms();
    if (usb_spectrum_packet_seen && now - usb_spectrum_packet_ms >= 100u)
        usb_input_spectrum_stream_start();
    usb_spectrum_packet_ms = now;
    usb_spectrum_packet_seen = 1;
}

static void usb_input_spectrum_push(int32_t l, int32_t r)
{
    uint32_t at = usb_spectrum_count & (USB_SPECTRUM_RING - 1u);
    usb_spectrum_ring[at][0] = (int16_t)(l >> 8);
    usb_spectrum_ring[at][1] = (int16_t)(r >> 8);
    if (usb_spectrum_filled < USB_SPECTRUM_RING) usb_spectrum_filled++;
    usb_spectrum_count++;
}

int plat_usb_input_spectrum(int16_t (*out)[2], uint32_t *position)
{
    uint32_t lock = plat_lock(), end = usb_spectrum_count;
    uint32_t generation = usb_spectrum_generation, i, k;
    int valid = usb_spectrum_enabled && usb_spectrum_filled >= PLAT_SPECTRUM_FRAMES &&
        usb_spectrum_packet_seen && plat_ms() - usb_spectrum_packet_ms < 100u;
    plat_unlock(lock);
    if (!valid) return 0;
    /* Half a ring of headroom, and at most 32 stereo frames per IRQ lock.
     * Check both stream generation and overwrite age for every chunk. */
    for (i = 0; i < PLAT_SPECTRUM_FRAMES; i += 32u) {
        uint32_t start = end - PLAT_SPECTRUM_FRAMES + i;
        lock = plat_lock();
        if (generation != usb_spectrum_generation ||
                usb_spectrum_count - start > USB_SPECTRUM_RING) {
            plat_unlock(lock);
            return 0;
        }
        for (k = 0; k < 32u; k++) {
            uint32_t at = (start + k) & (USB_SPECTRUM_RING - 1u);
            out[i+k][0] = usb_spectrum_ring[at][0];
            out[i+k][1] = usb_spectrum_ring[at][1];
        }
        plat_unlock(lock);
    }
    lock = plat_lock();
    valid = generation == usb_spectrum_generation &&
        plat_ms() - usb_spectrum_packet_ms < 100u;
    plat_unlock(lock);
    if (valid) *position = end;
    return valid;
}
