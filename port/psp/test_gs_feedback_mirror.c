/* Regression for the demand-driven PS2 GS RAM feedback mirror.
 * The PSMT8 texture view consumes bytes of the SAME 4 MiB memory that
 * PSMCT32 writes. Never expand the GU RGB5551 output to guess RGB/A. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "gs_feedback_mirror.h"
int main(void) {
    uint8_t tracked[TP3_GS_VRAM_PAGES]={0};
    uint32_t stamps[TP3_GS_VRAM_PAGES]={0};
    uint32_t pageMem[64]={0};
    const uint32_t rgba=0x7f3812e9u;
    const uint8_t *byteView=(const uint8_t *)pageMem;
    unsigned p=PspGsMirrorPageOfBlock(336u*32u);
    assert(sizeof(pageMem)==256u);
    assert(TP3_GS_VRAM_BLOCKS*256u==4u*1024u*1024u);
    assert(TP3_GS_VRAM_PAGES*8192u==4u*1024u*1024u);
    assert(!PspGsMirrorShouldDraw(tracked,336));
    tracked[336]=1;
    assert(PspGsMirrorShouldDraw(tracked,336));
    assert(!PspGsMirrorShouldDraw(tracked,368));
    assert(!PspGsMirrorShouldDraw(tracked,512));
    assert(p==336u);
    assert(PspGsMirrorMarkPage(stamps,336u*32u,100u));
    assert(!PspGsMirrorMarkPage(stamps,336u*32u+15u,100u));
    assert(PspGsMirrorMarkPage(stamps,336u*32u,101u));
    assert(PspGsMirrorMarkPage(stamps,368u*32u,101u));
    assert(PspGsMirrorPageOfBlock(16384u)==0u);
    /* 32-bit render output retains all four 8-bit components. An
       indexed reinterpretation can consume any of their raw bytes,
       including a preserved 8-bit alpha channel. */
    pageMem[0]=rgba;
    assert(byteView[0]==0xe9u && byteView[1]==0x12u);
    assert(byteView[2]==0x38u && byteView[3]==0x7fu);
    /* An RGB5551 round-trip would lose the low 3 bits of RGB and
       seven of alpha, so cannot be used as a GS memory reconstruction. */
    assert((rgba&0xffu)!=((rgba&0xffu)&0xf8u));
    return 0;
}
