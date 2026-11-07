/* SPDX-License-Identifier: MPL-2.0 */
/* Main-thread scratch allocator. No allocation/free from an ISR. Owners must
 * detach interrupt consumers before freeing. Eight slots, no in-band headers;
 * released blocks and adjacent gaps are immediately reusable. */
#pragma once
#ifdef APP_DYNAMIC_DISPLAY
#ifdef PLAT_HOST
static uint64_t work_host_storage[192u * 1024u / 8u];
#define WORK_BASE ((uint8_t *)work_host_storage)
#define WORK_END (WORK_BASE + sizeof work_host_storage)
#else
extern uint32_t _pool_end[], _sram_end[];
#define WORK_BASE ((uint8_t *)(((uintptr_t)_pool_end + 7u) & ~(uintptr_t)7u))
/* Retain the same 8-KiB minimum headroom as the static build checks. */
#define WORK_END ((uint8_t *)_sram_end - 8192u)
#endif
static struct { uint8_t *base; uint32_t bytes; } work_slots[8];
uint32_t plat_work_available(void)
{
    uint32_t i, bytes = WORK_END > WORK_BASE ? (uint32_t)(WORK_END - WORK_BASE) : 0;
    for (i = 0; i < 8; i++) bytes -= work_slots[i].bytes;
    return bytes;
}
void *plat_work_alloc(uint32_t bytes)
{
    uint32_t i, slot = 8;
    uint8_t *at = WORK_BASE;
    if (!bytes || bytes > UINT32_MAX - 7u) return 0;
    bytes = (bytes + 7u) & ~7u;
    for (i = 0; i < 8; i++) if (!work_slots[i].base) { slot = i; break; }
    if (slot == 8) return 0;
    for (i = 0; i < 8;) {
        uint8_t *p = work_slots[i].base;
        if (p && p + work_slots[i].bytes > at &&
                (uintptr_t)p < (uintptr_t)at + bytes) {
            at = p + work_slots[i].bytes;
            i = 0;
        } else i++;
    }
    if (at > WORK_END || bytes > (uint32_t)(WORK_END - at)) return 0;
    work_slots[slot].base = at;
    work_slots[slot].bytes = bytes;
    return at;
}
void *plat_work_alloc_top(uint32_t bytes)
{
    uint32_t slot=8;
    if(!bytes || bytes>UINT32_MAX-7u)return 0;
    bytes=(bytes+7u)&~7u;
    if(WORK_END<WORK_BASE || bytes>(uint32_t)(WORK_END-WORK_BASE))return 0;
    for(uint32_t i=0;i<8;i++)if(!work_slots[i].base){slot=i;break;}
    if(slot==8)return 0;
    uint8_t *end=WORK_END;
    for(uint32_t i=0;i<8;) {
        uint8_t *p=work_slots[i].base;
        if(p && p<end && p+work_slots[i].bytes>end-bytes) {
            end=p;if(bytes>(uint32_t)(end-WORK_BASE))return 0;i=0;
        }else i++;
    }
    uint8_t *at=end-bytes;work_slots[slot].base=at;work_slots[slot].bytes=bytes;return at;
}
void plat_work_free(void *p)
{
    uint32_t i;
    for (i = 0; p && i < 8; i++) if (work_slots[i].base == p) {
        work_slots[i].base = 0;
        work_slots[i].bytes = 0;
        return;
    }
}
#endif
