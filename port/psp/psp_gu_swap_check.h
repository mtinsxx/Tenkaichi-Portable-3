#ifndef BT3_PSP_GU_SWAP_CHECK_H
#define BT3_PSP_GU_SWAP_CHECK_H
#include <stdint.h>

/* PSPSDK sceGuSwapBuffers returns the NEW drawing framebuffer pointer.
   It can be a VRAM-relative offset or an eDRAM mapped address; the PSP
   also exposes an uncached alias at 0x44000000. Recognize only our TWO
   legal 16-bit buffers, not arbitrary VRAM addresses or work targets. */
static inline int Psp_GuSwapOffset(uintptr_t value, unsigned framebufferSize) {
    uintptr_t rel = value;
    if (value >= (uintptr_t)0x04000000u && value < (uintptr_t)0x04200000u)
        rel = value - (uintptr_t)0x04000000u;
    else if (value >= (uintptr_t)0x44000000u && value < (uintptr_t)0x44200000u)
        rel = value - (uintptr_t)0x44000000u;
    if (rel == 0u) return 0;
    if (rel == (uintptr_t)framebufferSize) return (int)framebufferSize;
    return -1;
}
#endif
