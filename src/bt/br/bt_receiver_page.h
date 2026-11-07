/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <stdint.h>
#define BTR_NAME "FM1 BT Receiver"
static struct { volatile uint8_t on,pairing; uint8_t remote[6],allowed,setup,encrypted,key_kept;
    volatile uint8_t closing,central;uint8_t setup_sent,lt,reconnect_hold;uint16_t bit,fhs_bit;uint32_t offset;
    volatile uint8_t phase;uint32_t scan_at,started,close_at,secure_at,auth_at;
    volatile uint32_t last_good,noise,pages,fhs,good,bad,syncs,carries;
    uint32_t sync_clk;uint8_t sync_seen;uint16_t rxbit,status; } brl;
enum { BR_INQUIRY,BR_PAGE_SCAN,BR_PAGE_RESPONSE,BR_FHS_ACK,BR_CONNECTING,BR_LINKED };
