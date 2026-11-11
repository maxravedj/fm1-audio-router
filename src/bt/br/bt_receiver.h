/* SPDX-License-Identifier: MPL-2.0 */
/* A2DP receiver. CPU0 handles signalling/RTP; CPU1 decodes SBC/AAC/aptX/aptX HD/LDAC in the gateway. */
#pragma once
#include <stdint.h>
enum { BTR_IDLE, BTR_CONFIGURED, BTR_OPEN, BTR_STREAMING };
enum { BTR_CODEC_NONE, BTR_CODEC_SBC, BTR_CODEC_AAC, BTR_CODEC_APTX, BTR_CODEC_APTX_HD, BTR_CODEC_LDAC };
struct btr_profile { uint8_t state, media, codec, seid, conf[11]; };
static const uint8_t BTR_CAPS[4] = {0x3f, 0xff, 2, 250}; /* 44.1/48, all SBC modes/blocks/subbands/allocation */
uint32_t btr_signal(struct btr_profile *s, const uint8_t *p, uint32_t n, uint8_t out[64]);
uint32_t btr_frame_size(const uint8_t *p, uint32_t n, const uint8_t conf[4]);
void btr_audio_reset(uint32_t rate); /* caller excludes audio IRQ for this small reset */
void btr_media_reset(void);
void btr_media(const uint8_t *p, uint32_t n, const uint8_t conf[4]);
void btr_pcm_mix(int32_t *lr, uint32_t n, uint32_t gain);
struct btr_counters { uint32_t packets, frames, malformed, decoder_errors, gaps, underruns, overruns; };

#ifdef APP_AUDIO_MONITOR
void plat_audio_monitor_reset(int source);
void plat_audio_monitor_bt_begin(void);
void plat_audio_monitor_bt_sample(int32_t l,int32_t r);
#endif
