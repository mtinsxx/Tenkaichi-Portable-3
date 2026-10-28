#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_gs_screen_coherence.h"
int main(void){
    const uintptr_t f0=(uintptr_t)0x44000000u;
    const uintptr_t f1=(uintptr_t)0x44044000u;
    const uintptr_t wk=(uintptr_t)0x440cc000u;
    assert(!PspGsScreen_WritesTarget(0)); /* TEX0 lookup must not migrate */
    assert(PspGsScreen_WritesTarget(1));   /* FRAME draw can migrate */
    assert(PspGsScreen_Slot(f0,f0,f1)==0);
    assert(PspGsScreen_Slot(f1,f0,f1)==1);
    assert(PspGsScreen_Slot(wk,f0,f1)==-1);
    assert(!PspGsScreen_ChangesOwner(112u,112u));
    assert(PspGsScreen_ChangesOwner(0u,112u));
    assert(PspGsScreen_ChangesOwner(UINT32_MAX,0u));
    assert(PspGsScreen_CanRestore(1,0,1,9u,9u,5u,5u));
    assert(!PspGsScreen_CanRestore(0,0,1,9u,9u,5u,5u));
    assert(!PspGsScreen_CanRestore(1,1,1,9u,9u,5u,5u));
    assert(!PspGsScreen_CanRestore(1,0,0,9u,9u,5u,5u));
    assert(!PspGsScreen_CanRestore(1,0,1,8u,9u,5u,5u));
    assert(!PspGsScreen_CanRestore(1,0,1,9u,9u,4u,5u));
    assert(!PspGsScreen_CanRestore(1,0,1,0u,0u,5u,5u));
    puts("PSP GS source lookup, screen ownership and safe snapshot restoration: passed");
    return 0;
}
