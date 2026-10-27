#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_gu_scissor.h"
#include "psp_gs_frame_mask.h"
int main(void) {
    PspGuClip c;
    uint64_t frame32=(uint64_t)0u<<24;
    uint64_t frame24=(uint64_t)1u<<24;
    uint64_t frame16=(uint64_t)2u<<24;
    uint64_t frame16s=(uint64_t)10u<<24;
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
    assert(PspGs_FrameMaskToGu(frame16)==0u);
    assert(PspGs_FrameMaskToGu(frame16s)==0u);
    assert(PspGs_FrameMaskToGu(frame16|((uint64_t)0xFFFFFFFFu<<32))==0x80F8F8F8u);
    assert(PspGs_FrameMaskToGu(frame16s|((uint64_t)0xFFFFFFFFu<<32))==0x80F8F8F8u);
    assert(PspGs_FrameMaskToGu(frame16|((uint64_t)0xFF000000u<<32))==0x80000000u);
    assert(PspGs_FrameMaskToGu(frame16|((uint64_t)0x00FFFFFFu<<32))==0x00F8F8F8u);
    /* All one-bit GS FBMSK inputs must match the 16-bit GS mask
       projection in gs_core.c, for both PSMCT16 and 16S. */
    {
        unsigned bit;
        for(bit=0;bit<32u;bit++) {
            uint32_t mask=(uint32_t)1u<<bit;
            uint32_t m16=((mask>>3)&31u)|((mask>>6)&0x3E0u)|
                         ((mask>>9)&0x7C00u)|((mask>>16)&0x8000u);
            uint32_t expected=((m16&31u)<<3)|(((m16>>5)&31u)<<11)|
                              (((m16>>10)&31u)<<19)|
                              ((m16&0x8000u)?0x80000000u:0u);
            assert(PspGs_FrameMaskToGu(frame16|((uint64_t)mask<<32))==expected);
            assert(PspGs_FrameMaskToGu(frame16s|((uint64_t)mask<<32))==expected);
        }
    }
    assert(!PspGs_FrameMaskPartial(0u));
    assert(!PspGs_FrameMaskPartial(0x00FFFFFFu));
    assert(PspGs_FrameMaskPartial(0x00FF0000u));
    puts("PS2 GS inclusive scissor -> PSPSDK width/height, FRAME.FBMSK -> RGB5551 passed");
    return 0;
}
