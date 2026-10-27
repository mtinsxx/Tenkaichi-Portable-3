/* Portable arithmetic test for the GS upper-byte/nibble palette formats.
   The PSP decoder uses these helpers over kCol32-swizzled GS blocks.
   This proves bit selection, not scene-level visual correctness. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "gs_hi_tex.h"

int main(void) {
    static const uint32_t samples[] = {
        0x00000000u, 0xFFFFFFFFu, 0xABCD1234u,
        0x013579Bdu, 0x10203040u, 0xFECCBA98u
    };
    unsigned p, i;
    for (p = 0; p < 64; ++p) {
        int ok = p == 0x1B || p == 0x24 || p == 0x2C;
        assert(Psp_GsIsHighIndexed(p) == ok);
    }
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        uint32_t word = samples[i];
        assert(Psp_GsHighIndex(0x1B, word) == (uint8_t)(word >> 24));
        assert(Psp_GsHighIndex(0x24, word) == (uint8_t)((word >> 24) & 15u));
        assert(Psp_GsHighIndex(0x2C, word) == (uint8_t)((word >> 28) & 15u));
    }
    for (i = 0; i < 256u; i++) {
        uint32_t word = (uint32_t)i << 24 | 0x00FEDCBAu;
        assert(Psp_GsHighIndex(0x1B, word) == i);
        assert(Psp_GsHighIndex(0x24, word) == (i & 15u));
        assert(Psp_GsHighIndex(0x2C, word) == (i >> 4));
    }
    puts("GS high-index PSMT8H/4HL/4HH extraction passed");
    return 0;
}
