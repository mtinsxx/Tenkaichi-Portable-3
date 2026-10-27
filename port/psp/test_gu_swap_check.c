#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_gu_swap_check.h"
int main(void) {
    enum { FB_SIZE = 512 * 272 * 2, DEPTH_OFF = 2 * FB_SIZE };
    assert(Psp_GuSwapOffset((uintptr_t)0, FB_SIZE) == 0);
    assert(Psp_GuSwapOffset((uintptr_t)FB_SIZE, FB_SIZE) == FB_SIZE);
    assert(Psp_GuSwapOffset((uintptr_t)0x04000000u, FB_SIZE) == 0);
    assert(Psp_GuSwapOffset((uintptr_t)0x04000000u + FB_SIZE, FB_SIZE) == FB_SIZE);
    assert(Psp_GuSwapOffset((uintptr_t)0x44000000u, FB_SIZE) == 0);
    assert(Psp_GuSwapOffset((uintptr_t)0x44000000u + FB_SIZE, FB_SIZE) == FB_SIZE);
    assert(Psp_GuSwapOffset((uintptr_t)DEPTH_OFF, FB_SIZE) == -1);
    assert(Psp_GuSwapOffset((uintptr_t)0x04000000u + DEPTH_OFF, FB_SIZE) == -1);
    assert(Psp_GuSwapOffset((uintptr_t)0x04100000u, FB_SIZE) == -1);
    assert(Psp_GuSwapOffset((uintptr_t)0x12345678u, FB_SIZE) == -1);
    puts("PSP GU swap return normalization / legal framebuffer test passed");
    return 0;
}
