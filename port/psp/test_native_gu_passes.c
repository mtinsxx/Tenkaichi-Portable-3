#include <assert.h>
#include <stdio.h>
#include "psp_native_gu_passes.h"
int main(void) {
 assert(!PspGuPass_MustFlush(PSP_GU_PASS_STAGE,PSP_GU_PASS_FIGHTER,0));
 assert(!PspGuPass_MustFlush(PSP_GU_PASS_STAGE,PSP_GU_PASS_STAGE,6));
 assert(PspGuPass_MustFlush(PSP_GU_PASS_STAGE,PSP_GU_PASS_FIGHTER,6));
 assert(PspGuPass_MustFlush(PSP_GU_PASS_FIGHTER,PSP_GU_PASS_SPRITE,3));
 assert(PspGuPass_MustFlush(PSP_GU_PASS_SPRITE,PSP_GU_PASS_EFFECT,6));
 assert(!PspGuPass_MustFlush(PSP_GU_PASS_NONE,PSP_GU_PASS_STAGE,3));
 assert(!PspGuPass_MustFlush(PSP_GU_PASS_STAGE,PSP_GU_PASS_NONE,3));
 assert(PspGuPass_Next(PSP_GU_PASS_FIGHTER)==PSP_GU_PASS_FIGHTER);
 assert(PspGuPass_Next(999u)==PSP_GU_PASS_NONE);
 puts("PSP native GU stages/actors/sprites preserve ordered barriers");
 return 0;
}
