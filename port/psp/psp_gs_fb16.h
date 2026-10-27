/* PSP colour-buffer compatibility subset for GS PSMCT16 output.
 * PS2 PSMCT16 (PSM 0x02) and PSP GU_PSM_5551 both use R5 G5 B5 A1
 * in a 16-bit pixel. The framebuffer's swizzled *memory layout*
 * differs; only direct rasterization into PSP GU's linear target is
 * supported by this initial port. Do not claim readback equivalence.
 */
#ifndef BT3_PSP_GS_FB16_H
#define BT3_PSP_GS_FB16_H
#include <stdint.h>

static inline int Psp_GsCanRenderColor16(uint32_t framePsm,
                                        uint32_t colorFbp,
                                        uint32_t depthFbp,
                                        int enable) {
    /* GS PSMCT16S=0x0A uses a different layout: leave on legacy path.
       The existing renderer also forbids color and depth aliases. */
    return enable && framePsm == 0x02u && colorFbp != depthFbp &&
           colorFbp < 512u && depthFbp < 512u;
}

static inline uint16_t Psp_GsColor16(uint8_t r, uint8_t g,
                                     uint8_t b, uint8_t a) {
    return (uint16_t)((r >> 3) | ((uint16_t)(g >> 3) << 5) |
                      ((uint16_t)(b >> 3) << 10) |
                      ((uint16_t)(a >= 128) << 15));
}
#endif
