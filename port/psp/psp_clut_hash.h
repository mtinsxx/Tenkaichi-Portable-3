/* PSP GS cache: original row-major CLUT FNV key, independent of the
   swizzled/block memory reader that fills the 16x1 or 16x16 rectangle. */
#ifndef BT3_PSP_CLUT_HASH_H
#define BT3_PSP_CLUT_HASH_H
#include <stdint.h>
static inline uint32_t Psp_ClutHashPixels(const uint32_t *pixels,
                                           unsigned count) {
    uint32_t h=2166136261u;
    unsigned i;
    for (i=0;i<count;i++)
        h=(h^pixels[i])*16777619u;
    return h;
}
#endif
