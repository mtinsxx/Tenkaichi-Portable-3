#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_gu_scissor.h"
#include "psp_gs_frame_mask.h"
int main(void) {
    PspGuClip c;
    uint64_t frame32=(uint64_t)0u<<24;
    uint64_t frame24=(uint64_t)1u<<24;
    c=PspGu_FromGs(0,0,512,448);
    assert(c.x==0 && c.y==0 && c.w==480 && c.h==272);
    c=PspGu_FromGs(100,100,100,100);
    assert(c.x==93 && c.w==95 && c.y==60 && c.h==62);
    /* Before fix code called sceGuScissor(93,60,188,122):
       188x122 instead of correct 95x62. */
    c=PspGu_FromGs(450,400,62,48);
    assert(c.x>=0 && c.y>=0 && c.x+c.w==480 && c.y+c.h==272);
    c=PspGu_FromGs(512,0,1,1);
    assert(c.w==0 && c.h==0);
    c=PspGu_FromGs(10,10,0,10);
    assert(c.w==0);
    assert(PspGs_FrameMaskToGu(frame32)==0u);
    assert(PspGs_FrameMaskToGu(frame32|((uint64_t)0x00FFFFFFu<<32))==0x00F8F8F8u);
    assert(PspGs_FrameMaskToGu(frame32|((uint64_t)0xFF000000u<<32))==0x80000000u);
    assert(PspGs_FrameMaskToGu(frame32|((uint64_t)0xFFFFFFFFu<<32))==0x80F8F8F8u);
    assert(PspGs_FrameMaskToGu(frame24)==0xFF000000u);
    assert(!PspGs_FrameMaskPartial(0u));
    assert(!PspGs_FrameMaskPartial(0x00FFFFFFu));
    assert(PspGs_FrameMaskPartial(0x00FF0000u));
    puts("PS2 GS inclusive scissor -> PSPSDK width/height, FRAME.FBMSK -> RGB5551 passed");
    return 0;
}
