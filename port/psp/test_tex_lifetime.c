#include <assert.h>
#include <stdio.h>
#include "psp_tex_lifetime.h"
int main(void) {
    /* Previous frame's GE list is already completed by FrameEnd. */
    assert(!Psp_TexNeedsGeFence(10u, 11u));
    assert(!Psp_TexNeedsGeFence(1u, 150u));
    /* A just-used texture can still be referenced by a pending batch. */
    assert(Psp_TexNeedsGeFence(11u, 11u));
    assert(Psp_TexNeedsGeFence(12u, 11u)); /* invalid/future, fence */
    assert(Psp_TexRamBudget(10u<<20, 0) == (10u<<20));
    assert(Psp_TexRamBudget(10u<<20,
           PSP_TEX_SCRATCH_RGBA_BYTES + PSP_TEX_SCRATCH_INDEX_BYTES)
           == (10u<<20) - (5u<<18));
    assert(Psp_TexRamBudget(10, 50) == 0);
    assert(Psp_TexUseScratch(256u*256u*4u, PSP_TEX_SCRATCH_RGBA_BYTES));
    assert(Psp_TexUseScratch(512u*512u, PSP_TEX_SCRATCH_INDEX_BYTES));
    assert(!Psp_TexUseScratch(512u*512u*4u+1u, PSP_TEX_SCRATCH_RGBA_BYTES));
    assert(!Psp_TexUseScratch(256, 0));
    puts("PSP GS texture old-frame eviction and persistent scratch rules passed");
    return 0;
}
