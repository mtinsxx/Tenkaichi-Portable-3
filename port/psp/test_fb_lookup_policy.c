#include <assert.h>
#include <stdio.h>
#include "psp_fb_lookup_policy.h"
int main(void) {
    assert(!PspFbLookup_CanChangeBacking(0));
    assert(PspFbLookup_CanChangeBacking(1));
    puts("GS framebuffer TEX0 lookup stays read-only");
    return 0;
}
