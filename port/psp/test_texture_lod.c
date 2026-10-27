/* Test the same in-place downsampling helpers used by psp_gu.c.
   Host-only test, no licensed game data or PSP SDK required. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "texture_lod.h"

static void test32(unsigned w, unsigned h, unsigned cap) {
    uint32_t rgba[512];
    unsigned sw = Psp_LodDimension(w, cap);
    unsigned sh = Psp_LodDimension(h, cap);
    unsigned x, y;
    assert(w * h <= sizeof(rgba) / sizeof(rgba[0]));
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
            rgba[y * w + x] = ((uint32_t)y << 16) | (uint32_t)x;
    Psp_LodSubsample32(rgba, w, h, sw, sh);
    for (y = 0; y < sh; ++y)
        for (x = 0; x < sw; ++x)
            assert(rgba[y * sw + x] ==
                   (((uint32_t)(y * (h / sh)) << 16) |
                    (uint32_t)(x * (w / sw))));
}

static void test8(unsigned w, unsigned h, unsigned cap) {
    uint8_t idx[512];
    unsigned sw = Psp_LodDimension(w, cap);
    unsigned sh = Psp_LodDimension(h, cap);
    unsigned x, y;
    assert(w * h <= sizeof(idx));
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
            idx[y * w + x] = (uint8_t)(x + 17u * y);
    Psp_LodSubsample8(idx, w, h, sw, sh);
    for (y = 0; y < sh; ++y)
        for (x = 0; x < sw; ++x)
            assert(idx[y * sw + x] ==
                   (uint8_t)((x * (w / sw)) + 17u * (y * (h / sh))));
}

int main(void) {
    unsigned w, h;
    for (w = 8; w <= 32; w *= 2)
        for (h = 8; h <= 16; h *= 2) {
            test32(w, h, 0);
            test8(w, h, 0);
            test32(w, h, 8);
            test8(w, h, 8);
            test32(w, h, 16);
            test8(w, h, 16);
        }
    assert(Psp_LodDimension(512, 256) == 256);
    assert(Psp_LodDimension(512, 128) == 128);
    assert(Psp_LodDimension(256, 128) == 128);
    assert(Psp_LodDimension(64, 256) == 64);
    assert(Psp_LodDimension(512, 0) == 512);
    /* 512x512 5551 = 512 KiB; capped to 256x256 = 128 KiB,
       capped to 128x128 = 32 KiB (GPU cache copy only). */
    assert((512u * 512u * 2u) == 524288u);
    assert((256u * 256u * 2u) == 131072u);
    assert((128u * 128u * 2u) == 32768u);
    puts("PSP 3D texture LOD in-place 8/32-bit sampling tests passed");
    return 0;
}
