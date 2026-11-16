/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <stdint.h>
#define BTR_NAME "FM1 BT Receiver"
#ifdef APP_BT_MULTIPOINT
#define BR_LINKS 2u
/* CPU0 foreground selects a context; radio IRQ wrappers save/restore it.
 * CPU1 never accesses signalling contexts. DMA keeps each link's own frame. */
static uint8_t br_current; /* IRQ wrappers restore it; no asynchronous changes escape an IRQ. */
static volatile uint8_t br_radio,br_switching;
static uint8_t br_audio_link=255,br_on,br_pairing;
static uint32_t br_slice_at;
static uint8_t br_held_peers[4][6],br_held_valid,br_held_next;
static unsigned br_peer_held(const uint8_t addr[6]);
static void br_hold_peer(int hold);
#else
#define BR_LINKS 1u
#define br_current 0u
#endif
static struct { volatile uint8_t on,pairing; uint8_t remote[6],allowed,setup,encrypted,key_kept;
    volatile uint8_t closing,central;uint8_t setup_sent,lt,reconnect_hold;uint16_t bit,fhs_bit;uint32_t offset;
    volatile uint8_t phase;uint32_t scan_at,started,close_at,secure_at,auth_at;
    volatile uint32_t last_good,noise,pages,fhs,good,bad,syncs,carries;
    uint32_t sync_clk,last_rx_clkn;uint8_t sync_seen,train,parity,generation;uint16_t rxbit,status; } br_links[BR_LINKS];
#define brl (br_links[br_current])
#define BR_FRAME() fm1_br_frame(bti_tab,br_current)
#define BR_RX_OFF (1088u+1360u*br_current)
enum { BR_INQUIRY,BR_PAGE_SCAN,BR_PAGE_RESPONSE,BR_FHS_ACK,BR_CONNECTING,BR_LINKED };
