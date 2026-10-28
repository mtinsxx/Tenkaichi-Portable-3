#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_stage399_empty.h"
static uint64_t tag(unsigned count,unsigned prim) {
    return ((uint64_t)count & 0x7FFFu) |
           (((uint64_t)prim & 7u) << 47u);
}
int main(void) {
    unsigned i;
    for(i=0;i<3u;i++) {
        assert(PspStage399_EmptyStrip(0,tag(i,4)));
        assert(PspStage399_EmptyStrip(1010u,tag(i,4)));
        assert(!PspStage399_EmptyStrip(1022u,tag(i,4)));
        assert(!PspStage399_EmptyStrip(900,tag(i,3)));
        assert(!PspStage399_EmptyStrip(900,tag(i,1)));
        assert(!PspStage399_EmptyStrip(900,tag(i,6)));
    }
    /* Counts of 3+ have visible triangles and are not shortcut. */
    assert(!PspStage399_EmptyStrip(0,tag(3,4)));
    assert(!PspStage399_EmptyStrip(0,tag(1024,4)));
    assert(!PspStage399_EmptyStrip(0,tag(0x7fff,4)));
    assert(PspStage399_EmptyStrip(1019,tag(0,4)));
    assert(PspStage399_EmptyStrip(1020,tag(0,4)));
    assert(!PspStage399_EmptyStrip(1024,tag(0,4)));
    puts("PSP stage399 strict triangle-strip empty-batch and packet bounds PASS");
    return 0;
}
