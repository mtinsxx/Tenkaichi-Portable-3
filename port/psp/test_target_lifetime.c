#include <assert.h>
#include <stdio.h>
#include "psp_target_lifetime.h"
int main(void) {
    int a, b, work;
    assert(PspTarget_RebindStillRendered(1,&a,&a));
    assert(!PspTarget_RebindStillRendered(0,&a,&a));
    assert(!PspTarget_RebindStillRendered(1,&a,&b));
    assert(!PspTarget_RebindStillRendered(1,&a,&work));
    assert(!PspTarget_RebindStillRendered(1,&work,&a));
    assert(!PspTarget_KeepAfterScreenClear(1,&a,&work));
    assert(!PspTarget_KeepAfterScreenClear(1,&b,&work));
    assert(PspTarget_KeepAfterScreenClear(1,&work,&work));
    assert(!PspTarget_KeepAfterScreenClear(0,&work,&work));
    puts("PSP screen/work target lifetime passed");
    return 0;
}
