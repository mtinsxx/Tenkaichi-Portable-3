#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_t8_native.h"
int main(void) {
    unsigned i;
    unsigned char found[256]={0};
    for(i=0;i<256;i++) {
        unsigned j=PspT8_Csm1Index(i);
        assert(j<256u && !found[j]);
        found[j]=1u;
    }
    assert(PspT8_Csm1Index(8u)==16u);
    assert(PspT8_Csm1Index(16u)==8u);
    assert(PspT8_Csm1Index(0u)==0u);
    assert(PspT8_GuPaletteColor(0x80030201u)==0xFF030201u);
    assert(PspT8_GuPaletteColor(0x40030201u)==0x80030201u);
    assert(PspT8_GuPaletteColor(0x00030201u)==0x00030201u);
    assert(PspT8_GuPaletteColor(0xFF030201u)==0xFF030201u);
    assert(PspT8_AllocatedBytes(256u,256u)==256u*256u+1024u);
    assert(PspT8_AllocatedBytes(8u,8u)==64u+1024u);
    assert(PspT8_AllocatedBytes(1u,1u)==64u+1024u);
    puts("PSP native T8 CLUT parity and compact storage passed");
    return 0;
}
