#include <assert.h>
#include <stdio.h>
#include "psp_hud_mask.h"
int main(void) {
    float a[3]={30,200,30}, b[3]={15,15,32};
    const unsigned TRISTRIP_BLEND_TEX=0x154, TRISTRIP_BLEND_SOLID=0x44;
    /* Every PS2 HudGfx_ClearAlpha sweep must reach the PSP renderer.
       The game performs one before each player's gauge! */
    for (unsigned strip=0; strip<16u; ++strip) {
        float left=(float)(strip*32u);
        assert(Psp_GsIsFullHudAlphaResetStrip(
            6u, 0x46u, 0x44u, 0x30000u,
            left, 0.f, left+32.f, 448.f, 0,0,0,0));
        /* Native GS fixed point can shift the bounds by .9375. */
        assert(Psp_GsIsFullHudAlphaResetStrip(
            6u, 0x46u, 0x44u, 0x30000u,
            left+0.9375f, .9375f, left+32.9375f, 448.9375f, 0,0,0,0));
    }
    assert(!Psp_GsIsFullHudAlphaResetStrip(
        6u, 0x46u, 0x44u, 0x30000u, 0, 0, 32, 64, 0,0,0,0));
    assert(!Psp_GsIsFullHudAlphaResetStrip(
        6u, 0x46u, 0x44u, 0x30000u, 0, 0, 32, 448, 1,0,0,0));
    assert(!Psp_GsIsFullHudAlphaResetStrip(
        6u, 0x154u, 0x44u, 0x30000u, 0, 0, 32, 448, 0,0,0,0));
    assert(!Psp_GsIsFullHudAlphaResetStrip(
        6u, 0x46u, 0x44u, 0x34000u, 0, 0, 32, 448, 0,0,0,0));
    assert(!Psp_GsIsFullHudAlphaResetStrip(
        6u, 0x46u, 0x44u, 0x30000u, 7, 0, 39, 448, 0,0,0,0));
    assert(Psp_GsIsTopHudTriangle(a,b));
    b[2]=145;
    assert(!Psp_GsIsTopHudTriangle(a,b));
    b[2]=32;
    assert(Psp_GsHudMaskClassify(0,0,0x30000,TRISTRIP_BLEND_SOLID,0x44,1)
            ==PSP_HUD_MASK_CLEAR);
    assert(Psp_GsHudMaskClassify(0xFFFFFF,1,0x3000F,TRISTRIP_BLEND_TEX,0x44,0)
            ==PSP_HUD_MASK_SET);
    assert(Psp_GsHudMaskClassify(0,0,0x34000,TRISTRIP_BLEND_TEX,0x44,0)
            ==PSP_HUD_MASK_DATE_ZERO);
    assert(Psp_GsHudMaskClassify(0,0,0x3C000,TRISTRIP_BLEND_TEX,0x44,0)
            ==PSP_HUD_MASK_DATE_ONE);
    assert(Psp_GsHudMaskClassify(0,0,0x30000,TRISTRIP_BLEND_TEX,0x44,0)
            ==PSP_HUD_MASK_NONE);
    assert(!Psp_GsHudMaskWritesStencil(PSP_HUD_MASK_DATE_ZERO));
    assert(Psp_GsHudMaskWritesStencil(PSP_HUD_MASK_SET));
    assert(Psp_GsHudMaskWritesStencil(PSP_HUD_MASK_CLEAR));
    puts("HUD GS FBMSK/FBA DATE stencil conversion classifier passed");
    return 0;
}
