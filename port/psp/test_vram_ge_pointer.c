#include <assert.h>
#include <stdint.h>
#include "psp_vram_address.h"

/* CPU uncached eDRAM must not leak into GE texture/transfer address bits.
 * The physical 2 MiB range is 0x04000000..0x041fffff.
 * This does not test actual rendered pixels; PPSSPP screenshots still needed. */
int main(void) {
    assert(PspVram_GeAddress(UINT32_C(0x44000000)) == UINT32_C(0x04000000));
    assert(PspVram_GeAddress(UINT32_C(0x44000040)) == UINT32_C(0x04000040));
    assert(PspVram_GeAddress(UINT32_C(0x441fffff)) == UINT32_C(0x041fffff));
    assert(PspVram_GeAddress(UINT32_C(0x04000000)) == UINT32_C(0x04000000));
    assert(PspVram_GeAddress(UINT32_C(0x041fffff)) == UINT32_C(0x041fffff));
    assert(PspVram_GeAddress(UINT32_C(0x44200000)) == UINT32_C(0x44200000));
    assert(PspVram_GeAddress(UINT32_C(0x48000000)) == UINT32_C(0x48000000));
    assert(PspVram_GeAddress(UINT32_C(0x08800000)) == UINT32_C(0x08800000));
    assert((uintptr_t)PspVram_GePointer((const void *)(uintptr_t)UINT32_C(0x440e0000)) ==
           UINT32_C(0x040e0000));
    return 0;
}
