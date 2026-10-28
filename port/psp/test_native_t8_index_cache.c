#include <assert.h>
#include <stdio.h>
#include "psp_native_t8_index_cache.h"
int main(void) {
    PspNativeT8Source c={0};
    assert(!PspNativeT8Source_Matches(&c,336,8,0xdeadbeef,128,128));
    PspNativeT8Source_Remember(&c,336,8,0xdeadbeef,128,128);
    assert(PspNativeT8Source_Matches(&c,336,8,0xdeadbeef,128,128));
    /* Changing the palette must not change the index source cache key. */
    assert(PspNativeT8Source_Matches(&c,336,8,0xdeadbeef,128,128));
    /* Any source page modification, address, stride or size change must
     * invalidate the previously converted indices. */
    assert(!PspNativeT8Source_Matches(&c,336,8,0xdeadbeee,128,128));
    assert(!PspNativeT8Source_Matches(&c,368,8,0xdeadbeef,128,128));
    assert(!PspNativeT8Source_Matches(&c,336,9,0xdeadbeef,128,128));
    assert(!PspNativeT8Source_Matches(&c,336,8,0xdeadbeef,256,128));
    assert(!PspNativeT8Source_Matches(&c,336,8,0xdeadbeef,128,0));
    assert(!PspNativeT8Source_Matches(&c,336,8,0xdeadbeef,128,513));
    c.valid=0;
    assert(!PspNativeT8Source_Matches(&c,336,8,0xdeadbeef,128,128));
    puts("PSP T8 native source index cache respects all GS source pages");
    return 0;
}
