/* Exact PS2 GS white-sprite MODULATE conversion, for GU_TRANSFORM_2D.
 * PS2: RGB = min(255, (textureRGB * vertexRGB) >> 7)
 * PSP GU: RGB = (textureRGB * vertexRGB) / 255.
 *
 * If the GS sprite's FLAT RGBAQ is exactly (255,255,255,128),
 * prebaking RGB into a separate PSP texture cache entry and binding
 * GU_TFX_REPLACE yields the PS2 result. Its alpha is unchanged:
 * GS MODULATE TCC=RGBA uses (texAlpha*128)>>7 = texAlpha;
 * TCC=RGB uses vertex alpha 128 (PSP=255).
 *
 * Never alter PS2 GS VRAM, a framebuffer alias, or non-white sprites.
 */
#ifndef BT3_PSP_GS_WHITE_MODULATE_H
#define BT3_PSP_GS_WHITE_MODULATE_H
#include <stdint.h>
static inline int Psp_GsIsWhiteModSprite(unsigned type, int textured,
                                         unsigned tfx, uint32_t lastRGBA) {
    return type == 6u && textured && tfx == 0u &&
           lastRGBA == 0x80FFFFFFu;
}
static inline unsigned Psp_GsWhiteChannel(unsigned texChannel) {
    unsigned x = (texChannel * 255u) >> 7;
    return x > 255u ? 255u : x;
}
static inline uint32_t Psp_GsWhitePixel(uint32_t rgba) {
    return (rgba & 0xFF000000u) |
           Psp_GsWhiteChannel(rgba & 0xFFu) |
           (Psp_GsWhiteChannel((rgba >> 8) & 0xFFu) << 8) |
           (Psp_GsWhiteChannel((rgba >> 16) & 0xFFu) << 16);
}
#endif
