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
    /* Real sequence: a stage's FIRST slow draw records version 1,
       then 90 cached native triangles update the GE target. The
       original capture stays at 1 and the guarded fighter copy
       rejects it. Capture the final version before fighter starts. */
    assert(PspStageCompose_NeedsFinalRecord(17,17,91,1,1));
    assert(!decision(17,17,91,1,stage,fighter,1,1,1,0));
    assert(decision(17,17,91,91,stage,fighter,1,1,1,0));
    assert(!PspStageCompose_NeedsFinalRecord(17,17,91,91,1));
    assert(PspStageCompose_NeedsFinalRecord(18,17,91,91,1));
    assert(PspStageCompose_NeedsFinalRecord(18,17,91,91,0));
    assert(!PspStageCompose_NeedsFinalRecord(18,17,0,91,1));
    /* Same physical target already contains the stage; do not
       overwrite it with a redundant/overlapping GPU copy. */
    assert(!decision(17,17,91,91,stage,stage,1,1,1,0));
    /* The second WORK owner can overwrite a completed stage in the
       same GE list. Only a matching immutable snapshot may then be
       composed, never a newer owner or an older GS generation. */
    assert(PspStageCompose_UseSnapshot(91,91,1,1,0,0));
    assert(PspStageCompose_UseSnapshot(91,91,1,1,0,1));
    assert(PspStageCompose_UseSnapshot(91,91,1,1,1,0));
    assert(!PspStageCompose_UseSnapshot(91,92,1,1,0,0));
    assert(!PspStageCompose_UseSnapshot(91,91,0,1,0,0));
    assert(!PspStageCompose_UseSnapshot(91,91,1,0,0,0));
    assert(!PspStageCompose_UseSnapshot(91,91,1,1,1,1));
    assert(!PspStageCompose_UseSnapshot(0,0,1,1,0,0));
    /* Once the immutable source is chosen, it is an ordinary GE copy
       and the original ownership check remains meaningful. */
    assert(decision(17,17,91,91,0x08810000u,fighter,1,1,1,0));
    assert(!decision(17,17,91,91,0x08810000u,fighter,1,1,0,0));
    puts("PSP stage -> fighter GE compositing guard tests passed");
    return 0;
}
