#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_stage_compose.h"
static int decision(unsigned now,unsigned stage,unsigned written,
                    unsigned recorded,uintptr_t src,uintptr_t dst,
                    int screen,int ready,int owned,int used) {
    return PspStageCompose_ShouldCopy(now,stage,written,recorded,
                                     src,dst,screen,ready,owned,used);
}
int main(void) {
    uintptr_t stage=UINT32_C(0x440CC000);
    uintptr_t fighter=UINT32_C(0x44000000);
    assert(decision(15,15,3,3,stage,fighter,1,1,1,0));
    assert(decision(15,14,3,3,stage,fighter,1,1,1,0));
    assert(!decision(15,13,3,3,stage,fighter,1,1,1,0));
    assert(!decision(15,15,3,3,stage,stage,1,1,1,0));
    assert(!decision(15,15,3,3,stage,fighter,0,1,1,0));
    assert(!decision(15,15,3,3,stage,fighter,1,0,1,0));
    assert(!decision(15,15,3,3,stage,fighter,1,1,0,0));
    assert(!decision(15,15,3,3,stage,fighter,1,1,1,1));
    assert(!decision(15,15,0,0,stage,fighter,1,1,1,0));
    assert(!decision(15,15,4,3,stage,fighter,1,1,1,0));
    assert(!decision(15,15,3,3,0,fighter,1,1,1,0));
    assert(!decision(15,15,3,3,stage,0,1,1,1,0));
    puts("PSP stage -> fighter GE compositing guard tests passed");
    return 0;
}
