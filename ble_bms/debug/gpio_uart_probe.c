// SPDX-License-Identifier: GPL-3.0-or-later
// RAM-only SWD diagnostic. Samples input levels; never changes GPIO outputs.
#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(a))
typedef struct {
    uint32_t magic, status, start, initial[2], masks[2];
    uint32_t counts[48], edges[48][128];
} capture_t;
__attribute__((noreturn,used)) void probe(void) {
    volatile capture_t *out=(volatile capture_t *)0x20021000;
    uint32_t previous[2]={REG(0x50000510)&out->masks[0],REG(0x50000810)&out->masks[1]};
    out->initial[0]=previous[0];out->initial[1]=previous[1];
    uint32_t start=REG(0xe0001004);out->start=start;out->magic=0x42505258;out->status=1;
    while((uint32_t)(REG(0xe0001004)-start)<192000000u) {
        uint32_t now=REG(0xe0001004)-start;
        for(unsigned port=0;port<2;port++) {
            uint32_t bits=REG(port?0x50000810:0x50000510)&out->masks[port];
            uint32_t changed=bits^previous[port];previous[port]=bits;
            while(changed) {
                unsigned bit=(unsigned)__builtin_ctz(changed),pin=port*32+bit;
                uint32_t count=out->counts[pin];out->counts[pin]=count+1;
                if(count<128)out->edges[pin][count]=now;
                changed&=changed-1;
            }
        }
    }
    out->status=2;
    __asm volatile("dsb\n bkpt #0");
    for(;;){}
}
