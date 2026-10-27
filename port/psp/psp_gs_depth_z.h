#ifndef PSP_GS_DEPTH_Z_H
#define PSP_GS_DEPTH_Z_H
#include <stdint.h>

/* gs_core.c emits GS XYZ2.Z as an unsigned 24-bit integer.
   GU_TRANSFORM_2D uses a direct 16-bit Z coordinate. Dropping the
   low eight bits maps the full PS2 depth range onto PSP depth range.
   The old (z >> 8) / 257 * 65535 expanded Z by 255 after shifting. */
static inline uint16_t Psp_GsDepthZ24ToGuZ16(uint32_t z24) {
    return (uint16_t)((z24 & UINT32_C(0x00FFFFFF)) >> 8);
}
#endif
