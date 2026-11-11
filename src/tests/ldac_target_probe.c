/* SPDX-License-Identifier: Apache-2.0 */
/* Standalone link/stack probe only; never packaged or run on a device. These
 * byte-loop libc stand-ins bound the probe's direct calls, not firmware cost. */
#include <stddef.h>
void *memset(void *dest, int value, size_t bytes)
{
    unsigned char *p=dest;
    while(bytes--)*p++=(unsigned char)value;
    return dest;
}
void *memcpy(void *dest, const void *source, size_t bytes)
{
    unsigned char *p=dest;const unsigned char *q=source;
    while(bytes--)*p++=*q++;
    return dest;
}
