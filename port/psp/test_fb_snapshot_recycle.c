#include <assert.h>
#include <stdio.h>
#include "psp_fb_snapshot_recycle.h"
static int allowed(int alloc,int used,unsigned valid,unsigned written,
                   unsigned saved,unsigned gen,unsigned currentGen,
                   unsigned copied,unsigned sampled,unsigned now){
    PspGsFbVersion v={0};
    v.valid=valid;v.written=written;v.saved=saved;
    v.sourceGen=gen;v.frame=copied;
    return PspGsFb_CanRecycleSnapshot(alloc,used,&v,gen,
                currentGen,sampled,now);
}
int main(void){
    /* Never clobber a live previous-frame image. */
    assert(!allowed(1,1,1,5,5,8,8,10,10,11));
    /* A new frame may read a previous owner using this same snapshot. */
    assert(!allowed(1,1,1,5,5,8,8,10,10,12));
    /* In-frame texture bindings and GE copies are unsafe to reuse. */
    assert(!allowed(1,1,0,6,5,8,8,10,12,12));
    assert(!allowed(1,1,1,5,5,8,9,12,11,12));
    assert(!allowed(1,1,1,5,5,8,8,10,12,12));
    /* An older source invalidated by GS upload is recyclable. */
    assert(allowed(1,1,1,5,5,8,9,10,10,12));
    assert(allowed(1,1,1,6,5,8,8,10,10,12));
    assert(allowed(1,0,0,0,0,0,0,0,0,12));
    assert(!allowed(0,0,0,0,0,0,0,0,0,12));
    puts("PSP snapshot recycling only on stale, synchronized sources");
    return 0;
}
