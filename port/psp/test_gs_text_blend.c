/* PSP GS font/menu fixed-alpha regression.
 * PS2 GS ALPHA.FIX is /128; PSP GU_FIX is /255.
 * Both use the same colour-difference blend equation when A=Cs,
 * B=Cd, C=FIX, D=Cd, common for text and 2D sprites.
 * Test actual pixel brightness, not only the helper's raw output.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "gs_experiments.h"

static int clamp_byte(int a) {
    if (a < 0) return 0;
    if (a > 255) return 255;
    return a;
}

static int ps2_fixed_pixel(int src, int dst, unsigned fix) {
    unsigned gsFix = fix >= 128u ? 128u : fix;
    return clamp_byte(dst + ((src - dst) * (int)gsFix) / 128);
}

static int psp_fixed_pixel(int src, int dst, unsigned fix) {
    unsigned guFix = Psp_GsFixToGu(fix);
    return clamp_byte(dst + ((src - dst) * (int)guFix) / 255);
}

int main(void) {
    static const int channels[] = {0, 1, 32, 64, 127, 128, 191, 248, 255};
    unsigned fix, src, dst;
    unsigned checks = 0;
    for (fix = 0; fix < 256u; ++fix) {
        for (src = 0; src < sizeof(channels)/sizeof(channels[0]); ++src) {
            for (dst = 0; dst < sizeof(channels)/sizeof(channels[0]); ++dst) {
                int a = ps2_fixed_pixel(channels[src],channels[dst],fix);
                int b = psp_fixed_pixel(channels[src],channels[dst],fix);
                assert(a >= b - 1 && a <= b + 1);
                ++checks;
            }
        }
    }
    /* White font over a blue dialog, FIX=0x80 => white (not grey). */
    assert(ps2_fixed_pixel(255, 23, 128) == 255);
    assert(psp_fixed_pixel(255, 23, 128) == 255);
    assert(psp_fixed_pixel(255, 23, 128) >
           clamp_byte(23 + (255 - 23)*128/255) + 110);
    /* FIX 0x40 is HALF coverage, not a quarter. */
    assert(psp_fixed_pixel(255, 0, 64) == 128);
    assert(psp_fixed_pixel(255, 0, 0) == 0);
    assert(psp_fixed_pixel(255, 0, 255) == 255);
    printf("PSP GS fixed-alpha font/menu: %u PS2-vs-GU pixel comparisons passed\n",
           checks);
    return 0;
}
