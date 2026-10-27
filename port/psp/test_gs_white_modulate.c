/* Independent PS2 GS shade() reference against the PSP's baked-white
   texture conversion. Tests exact 0x80FFFFFF sprite RGBAQ and alpha.
   No game data, GUI or PSP SDK required. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "gs_white_modulate.h"

static unsigned gs_white_rgb(unsigned texture) {
    unsigned v = (texture * 255u) >> 7; /* shade() in gs_core.c */
    return v > 255u ? 255u : v;
}
static unsigned psp_old_modulate_rgb(unsigned texture) {
    return texture; /* GU_MODULATE with saturated vertex RGB=255 */
}

int main(void) {
    unsigned c, a, tested = 0;
    assert(Psp_GsIsWhiteModSprite(6,1,0,0x80FFFFFFu));
    assert(!Psp_GsIsWhiteModSprite(3,1,0,0x80FFFFFFu));
    assert(!Psp_GsIsWhiteModSprite(6,0,0,0x80FFFFFFu));
    assert(!Psp_GsIsWhiteModSprite(6,1,1,0x80FFFFFFu));
    assert(!Psp_GsIsWhiteModSprite(6,1,0,0x40FFFFFFu));
    assert(!Psp_GsIsWhiteModSprite(6,1,0,0x80808080u));
    assert(Psp_GsWhiteChannel(0) == 0);
    assert(Psp_GsWhiteChannel(128) == 255);
    assert(Psp_GsWhiteChannel(255) == 255);
    for (a=0; a<256; ++a) {
        for (c=0; c<256; ++c) {
            uint32_t p = (a << 24) | (c << 16) | (c << 8) | c;
            uint32_t after = Psp_GsWhitePixel(p);
            unsigned ref = gs_white_rgb(c);
            assert((after >> 24) == a); /* critical: font antialiasing */
            assert((after & 255u) == ref);
            assert(((after >> 8) & 255u) == ref);
            assert(((after >> 16) & 255u) == ref);
            ++tested;
        }
    }
    /* The exact symptom: a middle-grey font atlas texel is WHITE in
       original GS modulation with white RGBAQ, but remains GREY when
       copied unchanged to PSP's /255 blend. */
    assert(gs_white_rgb(128) == 255);
    assert(psp_old_modulate_rgb(128) == 128);
    assert((Psp_GsWhitePixel(0x80808080u) & 0x00FFFFFFu) ==
           0x00FFFFFFu);
    puts("PS2 white-font GS /128 vs PSP baked texture: 65536 pixel/alpha comparisons passed");
    return 0;
}
