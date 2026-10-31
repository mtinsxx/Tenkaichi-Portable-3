#ifndef TP3_PSP_T4_NATIVE_H
#define TP3_PSP_T4_NATIVE_H
#include <stdint.h>
/* Native PSP GE GU_PSM_T4: two GS 4-bit palette indices per byte.
 * CLUT4 low nibble is the left/first pixel; high nibble second.
 * Only PSMT4 (0x14) is enabled: PSMT4HL/HH use the GS's upper
 * 32-bit storage lanes and must take the reference fallback.
 * width>=32, width multiple of 32 and height multiple of 8 permit
 * the same 16-byte x 8-row GE swizzling as other native textures. */
static inline int PspT4_NativeEligible(unsigned psm,unsigned width,
                                       unsigned height,int whiteSprite) {
    return psm==0x14u && !whiteSprite &&
           width>=32u && width<=512u &&
           (width&31u)==0u && height>=8u &&
           height<=512u && (height&7u)==0u;
}
static inline unsigned PspT4_IndicesBytes(unsigned width,unsigned height) {
    return (width*height)/2u;
}
static inline unsigned PspT4_AlignedBytes(unsigned width,unsigned height) {
    return (PspT4_IndicesBytes(width,height)+63u)&~63u;
}
static inline unsigned PspT4_AllocatedBytes(unsigned width,unsigned height) {
    /* Load 256 palette entries via the SAME GU_CLUT setup as T8,
     * but only the first 16 colors are addressable by a T4 pixel. */
    return PspT4_AlignedBytes(width,height)+1024u;
}
/* Packed in place is safe: each output byte replaces two source bytes
 * read at indices 2*i,2*i+1 and writes to index i only. */
static inline void PspT4_PackInPlace(uint8_t *bytes,unsigned pixelCount) {
    unsigned i;
    for(i=0u;i<pixelCount/2u;i++)
        bytes[i]=(uint8_t)((bytes[2u*i]&15u) |
                           ((bytes[2u*i+1u]&15u)<<4u));
}
static inline unsigned PspT4_Pixel(const uint8_t *bytes,unsigned index) {
    return (bytes[index>>1u]>>((index&1u)*4u))&15u;
}
#endif
