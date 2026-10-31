#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_mirror_depth_only.h"
#include "psp_gs_no_effect.h"

/* Model the reference GS final write permission independently:
 * framebuffer writes if the colour mask has any unmasked bits,
 * unless discarded by ATE=NEVER/KEEP or ZTE=1/ZNEVER.
 * Z writes if ZMSK=0 and the fragment survives the tests.
 * The native depth-only path is used only when the reference
 * framebuffer cannot be altered and the fragment's alpha is irrelevant.
 * Z memory still uses the unchanged byte-exact PS2 vram_rw. */
static int reference_zero_effect(uint64_t frame,uint64_t zbuf,uint64_t test){
    int colorPossible=(uint32_t)(frame>>32)!=UINT32_MAX;
    int zPossible=((zbuf>>32)&1u)==0u;
    if(!colorPossible && !zPossible)return 1;
    if((test&1u) && ((test>>1)&7u)==0u && ((test>>12)&3u)==0u)return 2;
    if(((test>>16)&1u) && ((test>>17)&3u)==0u)return 3;
    return 0;
}
int main(void){
    unsigned mask,zmsk,ate,atst,afail,zte,ztst;
    unsigned nativeCount=0,discardCount=0,checks=0;
    const uint32_t masks[]={0u,UINT32_MAX,0xffffffu,0xff000000u,
                            0x00ffffffu,0x0000ffffu};
    for(mask=0;mask<6;mask++)
    for(zmsk=0;zmsk<2;zmsk++)
    for(ate=0;ate<2;ate++)
    for(atst=0;atst<8;atst++)
    for(afail=0;afail<4;afail++)
    for(zte=0;zte<2;zte++)
    for(ztst=0;ztst<4;ztst++){
        uint64_t frame=((uint64_t)masks[mask]<<32)|0x00123456u;
        uint64_t zbuf=((uint64_t)zmsk<<32)|0x00000abcu;
        uint64_t test=(uint64_t)ate|((uint64_t)atst<<1)|
                      ((uint64_t)afail<<12)|((uint64_t)zte<<16)|
                      ((uint64_t)ztst<<17);
        int native=PspGsMirror_DepthOnly(frame,test,zbuf);
        int skipped=PspGs_GuaranteedNoEffect(frame,zbuf,test);
        int orig=reference_zero_effect(frame,zbuf,test);
        assert(native==((masks[mask]==UINT32_MAX) && !ate && !zmsk));
        assert(skipped==orig);
        assert(PspGsMirror_ZNever(test)==(zte && ztst==0u));
        if(native) {
            /* If there is a Z test and it is NEVER, it must be
               skipped even though it is otherwise Z-only. */
            assert((uint32_t)(frame>>32)==UINT32_MAX);
            assert((test&1u)==0u);
            assert(((zbuf>>32)&1u)==0u);
            ++nativeCount;
        }
        if(skipped)++discardCount;
        ++checks;
    }
    assert(nativeCount>0u && discardCount>0u);
    printf("PSP native depth-only + Z-NEVER parity PASS: %u GS MASK/Z/ATE/AFAIL combinations, native=%u skips=%u\n",
           checks,nativeCount,discardCount);
    return 0;
}
