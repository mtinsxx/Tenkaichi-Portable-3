#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_work_pair.h"
int main(void) {
    uint32_t owner[PSP_WORK_SURFACES] = {UINT32_MAX,UINT32_MAX};
    assert(PSP_WORK_SURFACES==2u);
    assert(PspWork_Slot(336u)==0u);
    assert(PspWork_Slot(368u)==1u);
    assert(PspWork_Slot(0u)==0u && PspWork_Slot(112u)==0u);
    owner[PspWork_Slot(336u)] = 336u;
    owner[PspWork_Slot(368u)] = 368u;
    assert(PspWork_OwnerAllowsSample(owner[0],336u));
    assert(PspWork_OwnerAllowsSample(owner[1],368u));
    assert(!PspWork_OwnerAllowsSample(owner[0],368u));
    owner[PspWork_Slot(400u)] = 400u; /* unrelated FBP shares work 0 */
    assert(!PspWork_OwnerAllowsSample(owner[0],336u));
    assert(PspWork_OwnerAllowsSample(owner[1],368u));
    puts("PSP two-work framebuffer lifetime / alias test passed");
    return 0;
}
