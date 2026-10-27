/* Bit-exact PSP movie YCbCr420 -> 480x256 RGB5551 output.
 *
 * Preserve all source samples selected by the previous MPEG2 direct
 * output path (x*512/480, y*448/256); only move the invariant
 * coordinate division outside the per-picture loop and reuse chroma
 * components for consecutive pixels selecting the same 4:2:0 sample.
 *
 * No MPEG entropy/IDCT, timing, brightness, UV or audio changes.
 * Pure C, testable against the previous implementation on the host.
 */
#ifndef BT3_PSP_MOVIE_5551_H
#define BT3_PSP_MOVIE_5551_H
#include <stdint.h>

static inline void Psp_Movie5551InitMap(uint16_t xmap[480],
                                        uint16_t ymap[256]) {
    unsigned i;
    for (i = 0; i < 480u; ++i) xmap[i] = (uint16_t)(i * 512u / 480u);
    for (i = 0; i < 256u; ++i) ymap[i] = (uint16_t)(i * 448u / 256u);
}

static inline void Psp_Movie5551Convert(uint16_t *dst, int pitch,
                  const uint8_t *yy, const uint8_t *cb, const uint8_t *cr,
                  int stride, const uint16_t xmap[480],
                  const uint16_t ymap[256],
                  const int32_t ty[256], const int32_t trv[256],
                  const int32_t tgu[256], const int32_t tgv[256],
                  const int32_t tbu[256], const uint8_t clip[1024]) {
    unsigned x, y;
    for (y = 0; y < 256u; ++y) {
        const unsigned sy = ymap[y];
        const uint8_t *py = yy + sy * stride;
        const uint8_t *pcb = cb + (sy >> 1) * (stride >> 1);
        const uint8_t *pcr = cr + (sy >> 1) * (stride >> 1);
        uint16_t *row = dst + y * pitch;
        unsigned previousChroma = 0xFFFFFFFFu;
        int redOffset = 0, greenOffset = 0, blueOffset = 0;
        for (x = 0; x < 480u; ++x) {
            unsigned sx = xmap[x], chroma = sx >> 1;
            if (chroma != previousChroma) {
                int d = pcb[chroma], e = pcr[chroma];
                redOffset = trv[e];
                greenOffset = tgu[d] + tgv[e];
                blueOffset = tbu[d];
                previousChroma = chroma;
            }
            {
                int c = ty[py[sx]];
                uint32_t red = clip[((c + redOffset) >> 16) + 384];
                uint32_t green = clip[((c + greenOffset) >> 16) + 384];
                uint32_t blue = clip[((c + blueOffset) >> 16) + 384];
                row[x] = (uint16_t)(0x8000u | (red >> 3) |
                                    ((green >> 3) << 5) |
                                    ((blue >> 3) << 10));
            }
        }
    }
}
#endif
