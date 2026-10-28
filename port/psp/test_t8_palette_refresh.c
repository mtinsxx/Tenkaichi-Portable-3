#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_t8_palette_refresh.h"
int main(void) {
    const uint64_t tex0=0x1111222233334444ull;
    const uint64_t texa=0xabcdu;
    PspT8PaletteIdentity t={tex0,texa,0x12345678u,128,128,3,1,0};
    assert(PspT8Palette_CanRefresh(&t,tex0,texa,0x12345678u,128,128,4));
    /* A current-frame draw may still be queued in the GE. */
    assert(!PspT8Palette_CanRefresh(&t,tex0,texa,0x12345678u,128,128,3));
    assert(!PspT8Palette_CanRefresh(&t,tex0,texa,0x12345678u,128,128,2));
    assert(!PspT8Palette_CanRefresh(&t,tex0+1,texa,0x12345678u,128,128,4));
    assert(!PspT8Palette_CanRefresh(&t,tex0,texa+1,0x12345678u,128,128,4));
    assert(!PspT8Palette_CanRefresh(&t,tex0,texa,0x12345679u,128,128,4));
    assert(!PspT8Palette_CanRefresh(&t,tex0,texa,0x12345678u,64,128,4));
    assert(!PspT8Palette_CanRefresh(&t,tex0,texa,0x12345678u,128,0,4));
    assert(!PspT8Palette_CanRefresh(&t,tex0,texa,0x12345678u,128,513,4));
    assert(!PspT8Palette_CanRefresh(NULL,tex0,texa,0x12345678u,128,128,4));
    t.whiteSprite=1;
    assert(!PspT8Palette_CanRefresh(&t,tex0,texa,0x12345678u,128,128,4));
    t.whiteSprite=0; t.indexed=0;
    assert(!PspT8Palette_CanRefresh(&t,tex0,texa,0x12345678u,128,128,4));
    puts("PSP T8 CLUT-only guarded refresh: correct source and GE-safe frame boundary PASS");
    return 0;
}
