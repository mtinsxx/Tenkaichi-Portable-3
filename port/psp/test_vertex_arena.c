/* Host regression for PSP GU vertex lifetime.
 * GU draws hold pointers (not snapshots), so queued spans must remain
 * disjoint until a real sceGuSync. No PSP headers/game files needed. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "psp_vertex_arena.h"

typedef struct {float u,v; uint32_t c; float x,y,z;} Vtx;
int main(void) {
    Vtx arena[64];
    unsigned committed=0u, pending=0u;
    unsigned offsets[3], counts[3]={7u,11u,5u};
    unsigned i,j;
    memset(arena,0,sizeof(arena));
    for(i=0u;i<3u;i++) {
        offsets[i]=committed;
        pending=counts[i];
        assert(PspGuVertexArena_Fits(committed,0u,pending,64u));
        for(j=0u;j<pending;j++) arena[committed+j].c=0xa000u+i;
        committed=PspGuVertexArena_Commit(committed,pending);
        pending=0u;
    }
    assert(committed==23u);
    for(i=0u;i<3u;i++)
        for(j=0u;j<counts[i];j++)
            assert(arena[offsets[i]+j].c==0xa000u+i);
    assert(PspGuVertexArena_Fits(23u,31u,10u,64u));
    assert(!PspGuVertexArena_Fits(23u,31u,11u,64u));
    assert(!PspGuVertexArena_Fits(65u,0u,1u,64u));
    assert(!PspGuVertexArena_Fits(63u,2u,0u,64u));
    assert(!PspGuVertexArena_CanReuse(0u));
    assert(PspGuVertexArena_CanReuse(1u));
    /* Real code fences the GE before the cursor is recycled. */
    assert(PSP_GU_VERTEX_FRAME_VERTICES>=PSP_GU_VERTEX_BATCH_VERTICES);
    assert(PSP_GU_VERTEX_BATCH_VERTICES==12288u);
    assert(PSP_GU_VERTEX_BATCH_VERTICES<65536u);
    assert(PSP_GU_VERTEX_BATCH_VERTICES%3u==0u);
    assert(PspGuVertexArena_Fits(0u,0u,
           PSP_GU_VERTEX_BATCH_VERTICES,PSP_GU_VERTEX_FRAME_VERTICES));
    assert(!PspGuVertexArena_Fits(
           PSP_GU_VERTEX_FRAME_VERTICES-PSP_GU_VERTEX_BATCH_VERTICES+1u,
           0u,PSP_GU_VERTEX_BATCH_VERTICES,PSP_GU_VERTEX_FRAME_VERTICES));
    return 0;
}
