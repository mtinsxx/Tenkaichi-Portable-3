#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_tex_lru.h"
int main(void) {
    /* All textures may have lastFrame=100. Evict the oldest READ,
       not whichever descriptor happens to occupy index zero. */
    uint32_t ticket[3] = {10u,11u,12u};
    assert(PspTexLru_IsOlder(ticket[0],ticket[1]));
    ticket[0]=13u; /* recent hot texture hit */
    assert(PspTexLru_IsOlder(ticket[1],ticket[0]));
    assert(PspTexLru_IsOlder(ticket[1],ticket[2]));
    /* Access-ticket wrap around must not turn a hot key into oldest. */
    assert(PspTexLru_IsOlder(UINT32_MAX-1u,0u));
    assert(!PspTexLru_IsOlder(1u,UINT32_MAX));
    assert(!PspTexLru_IsOlder(100u,100u));
    puts("PSP texture LRU access-order / wrap / frame fence separation passed");
    return 0;
}
