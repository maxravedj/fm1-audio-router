/* SPDX-License-Identifier: MPL-2.0 */
/* Exercise real BSP string encoding, with the app's TOML product macro supplied
 * by the focused runner. No invented VID/PID or USB interface changes. */
#define main existing_uac_main
#include "../../../bsp/tests/uac_test.c"
#undef main
int main(void)
{
    const uint8_t *p;uint16_t n;
    static const char *const want[]={"FM1 Audio"};
    for(unsigned i=0;i<1;i++){
        assert(get_desc(0x0302+i,&p,&n));
        assert(n==2+2*strlen(want[i]) && p[0]==n && p[1]==3);
        for(unsigned j=0;want[i][j];j++)assert(p[2+2*j]==want[i][j] && !p[3+2*j]);
    }
    test_descriptors();
    assert(!get_desc(0x0303,&p,&n)); /* app keeps the combined USB function */
    puts("PASS: actual BSP USB product UTF-16 descriptor: FM1 Audio; combined UAC1 interfaces and asynchronous feedback intact");
}
