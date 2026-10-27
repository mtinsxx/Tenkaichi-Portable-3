/* Fast VIF1 UNPACK for the PSP's native MIPS Allegrex CPU.
 *
 * Replaces the generic PS2 VIF component/bitpos interpreter for the
 * unmasked, MODE=0 Vn-8 and Vn-16 cases with WL <= CL.
 *
 * The caller MUST check those restrictions; all other VIF commands,
 * especially masking, row arithmetic and WL>CL fill writes, remain on
 * the original interpreter. This touches VU DATA memory (16 KiB),
 * not the original microprogram, gameplay state or GU render passes.
 *
 * PSP is little-endian like PS2; memcpy for 16-bit components also
 * avoids assuming the DMA buffer is aligned.
 */
#ifndef BT3_PSP_VIF_SUBWORD_H
#define BT3_PSP_VIF_SUBWORD_H
#include <stdint.h>
#include <string.h>

static inline void Psp_VifUnpackSubword(uint8_t *vuMem,
                                        const uint8_t *src,
                                        uint32_t addr, uint32_t count,
                                        uint32_t cl, uint32_t wl,
                                        uint32_t vn, uint32_t vl,
                                        uint32_t unsignedInput) {
    uint32_t k, cyc = 0;
    const uint32_t words = vn + 1u;
    const uint32_t bytesPerComponent = vl == 1u ? 2u : 1u;

    for (k = 0; k < count; k++) {
        uint32_t v[4] = {0, 0, 0, 0};
        uint32_t c;
        for (c = 0; c < words; c++) {
            uint32_t w;
            if (bytesPerComponent == 2u) {
                uint16_t h;
                memcpy(&h, src, sizeof(h));
                w = unsignedInput ? (uint32_t)h : (uint32_t)(int32_t)(int16_t)h;
            } else {
                uint8_t b = *src;
                w = unsignedInput ? (uint32_t)b : (uint32_t)(int32_t)(int8_t)b;
            }
            v[c] = w;
            src += bytesPerComponent;
        }
        if (vn == 0u) {
            v[1] = v[2] = v[3] = v[0];
        }
        memcpy(vuMem + ((addr & 0x3FFu) * 16u), v, sizeof(v));
        ++addr;
        if (++cyc == wl) {
            addr += cl - wl;
            cyc = 0;
        }
    }
}
#endif
