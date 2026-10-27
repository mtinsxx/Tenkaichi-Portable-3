#include <assert.h>
#include <stdio.h>
#include "psp_gs_static_skip.h"
int main(void) {
    PspGsStripSkip x={0};
    PspGsStripSkip_Set(&x,PSP_GS_STATIC_NONE);
    assert(!PspGsStripSkip_Next(&x));
    PspGsStripSkip_Set(&x,PSP_GS_STATIC_COLOR16);
    assert(PspGsStripSkip_Next(&x));
    assert(PspGsStripSkip_Next(&x));
    assert(x.skipped==2);
    assert(PspGsStripSkip_MaySkip(PSP_GS_STATIC_SPECIAL_TEXTURE));
    assert(!PspGsStripSkip_MaySkip(9u));
    /* Geometry clips and transient cache failures must NOT be marked. */
    x.kind=0;
    PspGsStripSkip_Set(&x,0u);
    assert(!PspGsStripSkip_Next(&x));
    puts("PSP native VU strip static format rejection memoization passed");
    return 0;
}
