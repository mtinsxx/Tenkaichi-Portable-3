#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_gs_fb_reconstruct.h"
static int near(float a,float b) {return a>=b-0.0001f && a<=b+0.0001f;}
int main(void) {
    PspGsFbVersion v={0};
    assert(PSP_GS_FB_SNAPSHOT_BYTES==524288u);
    assert(near(PspGsFb_PhysicalU(512.f),480.f));
    assert(near(PspGsFb_PhysicalV(448.f),272.f));
    assert(near(PspGsFb_PhysicalV(224.f),136.f));
    /* STQ samples the logical PS2 TEX0 dimensions, not the 512x512
       padded PSP GE transfer dimensions. */
    assert(near(PspGsFb_PhysicalU(0.5f*256.f),120.f));
    assert(near(PspGsFb_PhysicalV(0.5f*256.f),77.714286f));
    assert(near(PspGsFb_PhysicalV(1.f*512.f),310.85714f));
    assert(!PspGsFb_SampleValid(&v,1u));
    PspGsFb_OnDraw(&v);
    assert(PspGsFb_CanSave(&v,1u));
    PspGsFb_Saved(&v,1u,4u);
    assert(PspGsFb_SampleValid(&v,4u));
    assert(!PspGsFb_SampleValid(&v,5u));
    assert(!PspGsFb_CanSave(&v,1u));
    PspGsFb_OnDraw(&v);
    assert(!PspGsFb_SampleValid(&v,4u));
    assert(!PspGsFb_CanSave(&v,1u));
    assert(PspGsFb_CanSave(&v,2u));
    PspGsFb_Saved(&v,2u,5u);
    assert(PspGsFb_SampleValid(&v,5u));
    PspGsFb_Reset(&v);
    assert(!PspGsFb_SampleValid(&v,5u));
    puts("PSP virtual GS FB write-version, save lifetime, source gen and UV parity passed");
    return 0;
}
