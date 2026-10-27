/* Optional texture LOD for the PSP renderer.
 * Keep the game's GS memory untouched. Only the cached PSP copy of
 * triangle textures is reduced. 2D sprites, HUD and framebuffer
 * aliases must retain their original dimensions and coordinate space.
 */
#ifndef BT3_PSP_TEXTURE_LOD_H
#define BT3_PSP_TEXTURE_LOD_H
#include <stdint.h>

static inline unsigned Psp_LodDimension(unsigned source, unsigned cap) {
    return cap != 0 && source > cap ? cap : source;
}

static inline void Psp_LodSubsample32(uint32_t *pixels, unsigned srcW,
                                     unsigned srcH, unsigned dstW,
                                     unsigned dstH) {
    unsigned x, y, stepX = srcW / dstW, stepY = srcH / dstH;
    /* Power-of-two dimensions mean source indexes are >= destination
       indexes. Forward writes are therefore safe in the SAME buffer. */
    for (y = 0; y < dstH; y++) {
        const unsigned sourceRow = y * stepY * srcW;
        const unsigned destRow = y * dstW;
        for (x = 0; x < dstW; x++)
            pixels[destRow + x] = pixels[sourceRow + x * stepX];
    }
}

static inline void Psp_LodSubsample8(uint8_t *pixels, unsigned srcW,
                                    unsigned srcH, unsigned dstW,
                                    unsigned dstH) {
    unsigned x, y, stepX = srcW / dstW, stepY = srcH / dstH;
    for (y = 0; y < dstH; y++) {
        const unsigned sourceRow = y * stepY * srcW;
        const unsigned destRow = y * dstW;
        for (x = 0; x < dstW; x++)
            pixels[destRow + x] = pixels[sourceRow + x * stepX];
    }
}

#endif
