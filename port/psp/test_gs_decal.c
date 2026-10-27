/* Differential GS DECAL vs PSP GU texenv regression tests.
 * PS2 DECAL = texture RGB, independently of TCC and alpha.
 * With TCC=RGB, alpha comes from vertex; with TCC=RGBA it
 * comes from texture. sceGuTexFunc(GU_TFX_REPLACE, TCC) is exact,
 * unlike GU_TFX_DECAL for alpha textures.
 * Source: port/src/gs/gs_core.c shade(), PSPSDK sceGuTexFunc.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define GU_TFX_MODULATE 0
#define GU_TFX_DECAL 1
#define GU_TFX_BLEND 2
#define GU_TFX_REPLACE 3
#include "gs_experiments.h"

static unsigned to_psp_alpha(unsigned a) {
    return a >= 128u ? 255u : a * 2u;
}
static unsigned gs_decal_rgb(unsigned ct, unsigned cf, unsigned at) {
    (void)cf; (void)at; return ct;
}
static unsigned gu_replace_rgb(unsigned ct, unsigned cf, unsigned at) {
    (void)cf; (void)at; return ct;
}
static unsigned gu_decal_rgb(unsigned ct, unsigned cf, unsigned at) {
    return (cf * (255u - at) + ct * at + 127u) / 255u;
}
static unsigned composite(unsigned src, unsigned dst, unsigned a) {
    return (src * a + dst * (255u - a) + 127u) / 255u;
}
int main(void) {
    static const unsigned rgb[] = {0u, 16u, 55u, 96u, 128u, 175u, 220u, 248u, 255u};
    unsigned count = 0, tcc, tc, vc, ta, va;
    assert(Psp_GsTexFuncToGu(0) == GU_TFX_MODULATE);
    assert(Psp_GsTexFuncToGu(1) == GU_TFX_REPLACE);
    assert(Psp_GsTexFuncToGu(2) == GU_TFX_BLEND);
    assert(Psp_GsTexFuncToGu(3) == GU_TFX_BLEND);
    for (tcc=0; tcc<2; ++tcc) {
        for (tc=0; tc<sizeof(rgb)/sizeof(rgb[0]); ++tc) {
            for (vc=0; vc<sizeof(rgb)/sizeof(rgb[0]); ++vc) {
                for (ta=0; ta<=128; ta+=8) {
                    for (va=0; va<=128; va+=8) {
                        unsigned srcAlpha = to_psp_alpha(tcc ? ta : va);
                        unsigned gsRgb = gs_decal_rgb(rgb[tc], rgb[vc], ta);
                        unsigned pspRgb = gu_replace_rgb(rgb[tc], rgb[vc], to_psp_alpha(ta));
                        assert(gsRgb == pspRgb);
                        assert(composite(gsRgb, 0x13u, srcAlpha) ==
                               composite(pspRgb, 0x13u, srcAlpha));
                        ++count;
                    }
                }
            }
        }
    }
    /* Glyph texture: white with half coverage, grey tint vertex.
       GU_DECAL used to blend source RGB with the grey tint, losing
       more brightness BEFORE the real framebuffer alpha blend. */
    {
        unsigned texAlpha = to_psp_alpha(64u);
        unsigned oldRgb = gu_decal_rgb(255u, 55u, texAlpha);
        unsigned newRgb = gu_replace_rgb(255u, 55u, texAlpha);
        assert(newRgb == 255u && oldRgb < 165u);
        assert(composite(newRgb, 15u, texAlpha) >
               composite(oldRgb, 15u, texAlpha) + 40u);
        /* TCC=RGB ignores texture alpha and retains vertex alpha. */
        assert(to_psp_alpha(128) == 255u);
    }
    printf("GS DECAL to GU REPLACE: %u RGB/alpha comparisons passed\n",count);
    return 0;
}
