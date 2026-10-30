#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_gs_no_effect.h"

/* Exhaustively compare the shortcut against independent GS state
 * interpretation for the two classes with no possible memory writes.
 * No colour or depth-only operation may be lost. */
static int expected(uint64_t frame,uint64_t zbuf,uint64_t test) {
    int colourMasked=(uint32_t)(frame>>32)==UINT32_MAX;
    int depthMasked=((zbuf>>32)&1u)!=0u;
    int alphaNever=(test&1u)!=0u && ((test>>1)&7u)==0u;
    int failKeep=((test>>12)&3u)==0u;
    if(colourMasked && depthMasked) return 1;
    if(alphaNever && failKeep) return 2;
    return 0;
}
int main(void) {
    unsigned mask,zmask,ate,atst,afail,checks=0,seen[3]={0};
    const uint32_t masks[]={
        0u,UINT32_MAX,UINT32_C(0x00ffffff),UINT32_C(0xff000000),
        UINT32_C(0x0000ffff),UINT32_C(0xffff0000)
    };
    for(mask=0;mask<sizeof(masks)/sizeof(masks[0]);mask++)
    for(zmask=0;zmask<2;zmask++)
    for(ate=0;ate<2;ate++)
    for(atst=0;atst<8;atst++)
    for(afail=0;afail<4;afail++) {
        uint64_t frame=((uint64_t)masks[mask]<<32)|UINT64_C(0x00012345);
        uint64_t zbuf=((uint64_t)zmask<<32)|UINT64_C(0x98765432);
        uint64_t test=(uint64_t)ate | ((uint64_t)atst<<1) |
                      ((uint64_t)afail<<12) | UINT64_C(0x330000);
        int actual=PspGs_GuaranteedNoEffect(frame,zbuf,test);
        assert(actual==expected(frame,zbuf,test));
        assert(actual>=0 && actual<=2);
        ++seen[actual]; ++checks;
    }
    /* AFAIL_FB_ONLY/ZB_ONLY/RGB_ONLY must never be confused with
       alpha-test NEVER/KEEP: these write partial framebuffer planes. */
    for(afail=1;afail<4;afail++)
       assert(PspGs_GuaranteedNoEffect(0,0,UINT64_C(1)|((uint64_t)afail<<12))==0);
    assert(PspGs_GuaranteedNoEffect((UINT64_C(0xffffffff)<<32),0,
                                  UINT64_C(0))==0); /* Z-only */
    assert(PspGs_GuaranteedNoEffect(0,UINT64_C(1)<<32,
                                  UINT64_C(0))==0); /* colour-only */
    assert(checks==6u*2u*2u*8u*4u);
    assert(seen[0] && seen[1] && seen[2]);
    printf("PS2 GS GPU no-effect draw test PASS: %u state combinations; mask, z-write, AFAIL unchanged\n",checks);
    return 0;
}
