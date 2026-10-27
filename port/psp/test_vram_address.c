#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_vram_address.h"
int main(void) {
    assert(PspVram_Uncached(PSP_VRAM_CACHED_BASE)==PSP_VRAM_UNCACHED_BASE);
    assert(PspVram_Uncached(PSP_VRAM_CACHED_BASE+0x44000u)==
           PSP_VRAM_UNCACHED_BASE+0x44000u);
    assert(PspVram_Uncached(PSP_VRAM_UNCACHED_BASE)==PSP_VRAM_UNCACHED_BASE);
    assert(!PspVram_CanAliasIndexed(0x13u));
    assert(!PspVram_CanAliasIndexed(0x1bu));
    assert(!PspVram_CanAliasIndexed(0x24u));
    assert(!PspVram_CanAliasIndexed(0x2cu));
    assert(PspVram_CanAliasIndexed(0x00u));
    assert(PspVram_CanAliasIndexed(0x02u));
    puts("PSP VRAM uncached CPU alias and incompatible GS indexed-plane guard passed");
    return 0;
}
