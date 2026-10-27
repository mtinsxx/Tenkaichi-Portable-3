#ifndef BT3_PSP_HUD_MASK_H
#define BT3_PSP_HUD_MASK_H
#include <stdint.h>
/* PS2 HUD Gauge: HudSprite_DrawAt draws PRIM=0x154 TRIANGLE_STRIP.
   The GS backend dispatches its two triangles to GsGpu_Draw(type=3).
   GS FRAME_2 mask 0x00FFFFFF + FBA=1 write alpha only;
   TEST_1.DATE subsequently tests destination alpha's high bit.
   PSP RGB5551 has this bit in its 1-bit stencil channel. */
enum {
    PSP_HUD_MASK_NONE = 0,
    PSP_HUD_MASK_CLEAR = 1,
    PSP_HUD_MASK_SET = 2,
    PSP_HUD_MASK_DATE_ZERO = 3,
    PSP_HUD_MASK_DATE_ONE = 4
};
/* GS HudGfx_ClearAlpha (src/battle/hud_c_c.c) is emitted as sixteen
   PRIM=0x46 untextured sprites of 32x448, RGBA=(0,0,0,0), ALPHA=0x44.
   This clear is called between EACH player's gauges by Hud_Draw. It
   leaves RGB unchanged but MUST reset destination alpha/stencil. Do
   not replace it with a colour clear (or alter any generic sprites). */
static inline int Psp_GsIsFullHudAlphaResetStrip(
    unsigned type, unsigned prim, unsigned alpha, unsigned test,
    float x0, float y0, float x1, float y1,
    unsigned r, unsigned g, unsigned b, unsigned a) {
    float minx = x0 < x1 ? x0 : x1;
    float maxx = x0 > x1 ? x0 : x1;
    float miny = y0 < y1 ? y0 : y1;
    float maxy = y0 > y1 ? y0 : y1;
    float width = maxx - minx, height = maxy - miny;
    unsigned strip;
    if (type != 6 || (prim & 7u) != 6u ||
        ((prim >> 4) & 1u) != 0u || ((prim >> 6) & 1u) == 0u ||
        (alpha & 0xFFu) != 0x44u || (test & (1u << 14)) != 0u ||
        (r | g | b | a) != 0u)
        return 0;
    /* Permit the GS subpixel .9375 rounding, but only the 16 exact
       full-height strips at multiples of 32 inside the 512-wide screen. */
    if (minx < -1.0f || maxx > 514.0f || miny < -1.0f || miny > 2.0f ||
        width < 31.0f || width > 33.0f ||
        height < 447.0f || height > 449.0f)
        return 0;
    for (strip = 0; strip < 16; ++strip) {
        float left = (float)(strip * 32);
        if (minx >= left - 1.0f && minx <= left + 2.0f)
            return 1;
    }
    return 0;
}
static inline int Psp_GsIsTopHudTriangle(const float x[3],
                                          const float y[3]) {
    int i;
    float minx = x[0], maxx = x[0], miny = y[0], maxy = y[0];
    for (i = 0; i < 3; i++) {
        if (!(x[i] >= -1.f && x[i] <= 513.f &&
              y[i] >= -1.f && y[i] <= 110.f)) return 0;
        if (x[i] < minx) minx = x[i];
        if (x[i] > maxx) maxx = x[i];
        if (y[i] < miny) miny = y[i];
        if (y[i] > maxy) maxy = y[i];
    }
    return maxx > minx && maxy > miny;
}
static inline int Psp_GsHudMaskClassify(unsigned fbmsk,
                                        unsigned fba,
                                        unsigned test,
                                        unsigned prim,
                                        unsigned alpha,
                                        unsigned allZeroRGBA) {
    /* Color preserved, alpha cleared: HudSprite_DrawAlphaClear
       sends untextured black transparent PRIM=0x44, ALPHA=0x44. */
    if (allZeroRGBA && ((prim >> 4) & 1u) == 0u &&
        ((prim >> 6) & 1u) != 0u && (alpha & 0xFFu) == 0x44u &&
        (test & (1u << 14)) == 0u)
        return PSP_HUD_MASK_CLEAR;
    /* DRAW to GS FRAME_2 in alpha-only mode; set alpha's high bit only
       when texture and GS alpha test allow the pixel through. */
    if ((fbmsk & 0x00FFFFFFu) == 0x00FFFFFFu &&
        (fbmsk & 0xFF000000u) == 0u && (fba & 1u))
        return PSP_HUD_MASK_SET;
    /* GS TEST.DATE and DATM compare destination alpha MSB. */
    if (test & (1u << 14))
        return (test & (1u << 15)) ? PSP_HUD_MASK_DATE_ONE
                                   : PSP_HUD_MASK_DATE_ZERO;
    return PSP_HUD_MASK_NONE;
}
static inline int Psp_GsHudMaskWritesStencil(int mode) {
    return mode == PSP_HUD_MASK_CLEAR || mode == PSP_HUD_MASK_SET;
}
#endif
