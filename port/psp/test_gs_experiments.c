/* No PSP SDK required: compile this against the SAME inline helpers
   called by psp_gu.c. These are arithmetic tests, NOT pixel-level GS tests. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "gs_experiments.h"

static uint64_t tex0(unsigned psm, unsigned cbp, unsigned cld) {
    return ((uint64_t)psm << 20) | ((uint64_t)cbp << 37) |
           ((uint64_t)cld << 61) | 0x12345u;
}

int main(void) {
    const uint64_t mask = 0x1FFFFFE000000000ull;
    const uint64_t last = ((uint64_t)0x0371 << 37) |
                          ((uint64_t)2 << 51) |
                          ((uint64_t)11 << 56) | 0x9876ull;
    static const unsigned indexed[] = {0x13u, 0x14u, 0x1Bu, 0x24u, 0x2Cu};
    unsigned i, j;

    for (i = 0; i < 256; ++i) {
        unsigned expected = i >= 128u ? 255u : i * 2u;
        assert(Psp_GsFixToGu(i) == expected);
        assert(Psp_GsFixToGu(i + 256u) == expected);
        if (i && i < 128u)
            assert(Psp_GsFixToGu(i) >= Psp_GsFixToGu(i - 1u));
    }
    assert(Psp_GsFixToGu(0) == 0u);
    assert(Psp_GsFixToGu(127) == 254u);
    assert(Psp_GsFixToGu(128) == 255u);
    assert(Psp_GsFixToGu(255) == 255u);

    for (i = 0; i < sizeof(indexed) / sizeof(indexed[0]); ++i) {
        const uint64_t src = tex0(indexed[i], 0, 0);
        const uint64_t effective = Psp_GsEffectiveClut(src, last);
        assert((effective & mask) == (last & mask));
        assert((effective & ~mask) == (src & ~mask));
        assert(Psp_GsEffectiveClut(src, 0) == src);
        assert(Psp_GsEffectiveClut(tex0(indexed[i], 17, 0), last) ==
               tex0(indexed[i], 17, 0));
        for (j = 1; j < 8; ++j)
            assert(Psp_GsEffectiveClut(tex0(indexed[i], 0, j), last) ==
                   tex0(indexed[i], 0, j));
    }
    for (i = 0; i < 64; ++i) {
        if (i == 0x13u || i == 0x14u || i == 0x1Bu ||
            i == 0x24u || i == 0x2Cu)
            continue;
        assert(Psp_GsEffectiveClut(tex0(i, 0, 0), last) ==
               tex0(i, 0, 0));
    }
    puts("GS FIX/CLUT arithmetic and cache-key tests passed");
    return 0;
}
