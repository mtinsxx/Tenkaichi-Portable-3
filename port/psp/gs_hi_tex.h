/* PSP-only GS high indexed texture formats:
 * PSMT8H  (0x1B): high byte of a PSMCT32 word.
 * PSMT4HL (0x24): bits 24..27 of that word.
 * PSMT4HH (0x2C): bits 28..31 of that word.
 * These share the colour 32-bit pixel's GS block layout, NOT the
 * standalone PSMT8/PSMT4 byte/nibble swizzle.
 */
#ifndef BT3_PSP_GS_HI_TEX_H
#define BT3_PSP_GS_HI_TEX_H
#include <stdint.h>

static inline int Psp_GsIsHighIndexed(unsigned psm) {
    return psm == 0x1Bu || psm == 0x24u || psm == 0x2Cu;
}

static inline uint8_t Psp_GsHighIndex(unsigned psm, uint32_t word) {
    if (psm == 0x1Bu) return (uint8_t)(word >> 24);
    if (psm == 0x24u) return (uint8_t)((word >> 24) & 15u);
    if (psm == 0x2Cu) return (uint8_t)((word >> 28) & 15u);
    return 0;
}
#endif
