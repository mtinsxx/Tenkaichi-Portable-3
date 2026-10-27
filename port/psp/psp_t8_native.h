#ifndef TP3_PSP_T8_NATIVE_H
#define TP3_PSP_T8_NATIVE_H
#include <stdint.h>
/* PSMT8 is GS format 0x13 (decimal 19). PSMT8H is 0x1B (27)
   and must NOT be confused with it. GS CSM1 permutes the 256
   palette indices, and the PSP stores RGBA8888 palettes. The GS
   texture alpha range 0x80 is full intensity on PSP. */
static inline unsigned PspT8_Csm1Index(unsigned i) {
    return (i & 0xE7u) | ((i & 8u) << 1) | ((i & 0x10u) >> 1);
}
static inline uint32_t PspT8_GuPaletteColor(uint32_t c) {
    unsigned a = (c >> 24) & 255u;
    a = a >= 0x80u ? 255u : 2u*a;
    return (c & 0x00FFFFFFu) | ((uint32_t)a << 24);
}
static inline unsigned PspT8_AlignedBytes(unsigned tw, unsigned th) {
    return ((tw * th + 63u) & ~63u);
}
static inline unsigned PspT8_AllocatedBytes(unsigned tw,unsigned th) {
    return PspT8_AlignedBytes(tw, th) + 1024u;
}
#endif
