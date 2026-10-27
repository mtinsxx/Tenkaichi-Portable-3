#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_gs_afail.h"
static int pass(unsigned test,unsigned a,unsigned ref) {
    switch(test&7u) {
    case 0:return 0;
    case 1:return 1;
    case 2:return a<ref;
    case 3:return a<=ref;
    case 4:return a==ref;
    case 5:return a>=ref;
    case 6:return a>ref;
    default:return a!=ref;
    }
}
int main(void) {
    unsigned fn,a,ref,mode;
    for(fn=0;fn<8u;fn++)
        for(a=0;a<256u;a++)
            for(ref=0;ref<256u;ref++)
                assert(pass(fn,a,ref)!=pass(PspGs_InverseAtst(fn),a,ref));
    for(mode=0;mode<4u;mode++) {
        assert(PspGs_AfailNeedsReplay(0u,mode)==0u);
        assert(PspGs_AfailNeedsReplay(1u,mode)==(mode!=0u));
        assert(PspGs_AfailPassMask(mode)==(mode==2u?UINT32_C(0xFFFFFFFF):
                                          mode==3u?UINT32_C(0x80000000):0u));
        assert(PspGs_AfailWritesZ(mode)==(mode==2u));
    }
    puts("GS ATST 524288 inverse cases and all four AFAIL replay policies passed");
    return 0;
}
