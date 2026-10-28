<!-- SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 -->
# The own Wi-Fi stack and Ableton Link

`wifi_stack = "own"` (with `ble_stack = "own"`) builds fm1-base's own Wi-Fi: Henri David's from
[Optimist](https://github.com/w0ts/optimist) (its `firmware/src/wifi/`, `docs/WIFI-HW-FACTS.md`,
`docs/WIFI-LINK-HANDOVER.md`, `docs/LINK-DESIGN.md`: every measurement and how it was found). `link = true` adds
Ableton Link on it, GPL-3.0-only (LICENSING.md). How to use them: README.md, "WiFi on the own stack, and Ableton
Link"; the calls: `include/plat.h`.

## The parts

| Where | What |
| --- | --- |
| `bsp/chip/fm1_wifi.h` | the MAC (JieLi's wl30 core, Ralink RT3070's register layout at 0x30000 + r): the frame DMA (RX, TX), the channel, the address and key tables, the station driver's settings, `fm1_wifi_up` (the STA settings, both RF paths on, the coexistence tables) |
| `bsp/chip/fm1_wifi_wlc.h` | the coexistence controller's 565 set-up words (JieLi's, Apache-2.0): what made reception work on a radio Bluetooth had started |
| `wifi/wifi_crypto.c` | SHA-1, HMAC, PBKDF2 (the key from a password, in slices: 256 rounds a main-loop pass), the PRF, AES-128, key wrap / unwrap |
| `wifi/wifi_sta.c` | the WPA2-PSK station: authentication, association, the 4-way handshake and group rekeys, the keys into the MAC (it encrypts and decrypts CCMP itself) |
| `wifi/wifi_ip.c` | ARP, the DHCP client, IGMPv2 (one group), UDP (up to 4 ports), an early drop filter; as an access point (the simulator's): FromDS frames, ping, the DHCP server's hook |
| `wifi/wifi_nets.c` | the networks kept (four: SSID and key, never the password; the mode) and the scan's table (beacons parsed: WPA2-PSK with CCMP joinable, the rest listed) |
| `wifi/wifi_ap.c`, `wifi_ap_dhcp.c` | an access point's side (beacons, the management answers, the authenticator, a DHCP server): the host simulator's network runs them; no firmware builds them (an access point of the FM-1's own, MODE AP, is not part of this) |
| `device/wifi_own.c` | the radio's start (on Bluetooth's: `plat_ble_start` if needed), the connection manager (OFF / MAN / AUTO, the scan, the join and its retries, NETWORK LOST), the service once joined (the IP layer, the app's UDP ports), `plat_wifi_*`, `plat_net_udp_*` |
| `link/link.c`, `link/link_own.c` | Ableton Link (GPL-3.0-only): discovery, measurement, the session; `plat_link_*` |
| `host/wifi_fake/fm1_wifi.h`, `host/wifi_air.c` | the host simulator's radio and network: an access point, DHCP, a Link peer, all in the process |
| `tests/wifi/` | the host tests (`run.sh`): the crypto against published vectors, the station against the authenticator, the IP layer both ways, Link's wire format and session rules, the Link example on the simulated network |

## Timing and memory

- Everything runs from the main loop (`wifi_poll`, also from `plat_yield`): no task, no interrupt of its own. A
  received frame's arrival time is taken when the main loop fetches it (a UI pass of error at most).
- Link's clock is microseconds from `plat_ticks24` (TIMER4), with no 64-bit division; `plat_link_now` may be read in
  an interrupt. A MIDI clock from the session is the app's: a tick at `plat_link_time_at` of each 1/24 beat.
- The frame DMA reads main RAM only below 0x01C20000 (a buffer above went out as garbage: measured, the cause not
  proven): its two buffers (1.6 KB) are plain `.bss`, and the build checks their addresses in the map.
- 64-bit multiplies sit in a helper of their own (`lk_mul`): the build's check of felucca-bsp docs/CPU.md.

## What has been measured where

On an FM-1, in Optimist's firmware (the same code before the move; Optimist's docs/WIFI-TX-HANDOFF.md, 2026-10-27):
scan, join (WPA2-PSK), a DHCP lease, the IGMP join, Link's discovery on 224.76.78.75:20808 and Live synced, on the radio
Bluetooth started. Not on an FM-1: Wi-Fi beside Bluetooth audio streaming, the phase against
Live, a long soak, and this fm1-base build itself.
