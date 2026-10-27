#ifndef BT3_PSP_VRAM_ADDRESS_H
#define BT3_PSP_VRAM_ADDRESS_H
#include <stdint.h>
/* GE VRAM is accessible at 0x04000000 and through the CPU's uncached
 * alias at 0x44000000 (PSPSDK pspDebugScreenInitEx uses OR 0x40000000).
 * PSP backend CPU writes/uploads and direct post-GE reads must not
 * silently use the cached mapping.
 * GE draw/display/depth setup still takes OFFSETS, not these addresses. */
#define PSP_VRAM_CACHED_BASE UINT32_C(0x04000000)
#define PSP_VRAM_UNCACHED_BASE UINT32_C(0x44000000)
static inline uintptr_t PspVram_Uncached(uintptr_t addr) {
    return addr | (uintptr_t)UINT32_C(0x40000000);
}
/* A 5551 PSP surface is not an 8H/4HL/4HH palette-index buffer.
 * A 16-bit GPU backing cannot be reinterpreted as those 32-bit GS
 * storage planes. Fall back to GS VRAM decoding instead; if the
 * producer wrote only to the GPU, this needs an actual GS readback
 * and must not be misreported as a coherent framebuffer alias. */
/* GS colour texture interpretation must agree with its producing
 * framebuffer format before using a PSP 5551 snapshot. A 32-bit GS
 * framebuffer read through PSMCT16S (or even 24-bit TEXA) is a different
 * operation with its own swizzle and destination alpha. The cached GE
 * pixels cannot implement it. Only same-PSM views are admitted, and
 * only supported colour formats. All other views decode GS VRAM. */
static inline int PspVram_CanAliasGsPsm(uint32_t producer,
                                       uint32_t consumer) {
    return producer==consumer &&
           (producer==0u || producer==1u ||
            producer==2u || producer==10u);
}
static inline int PspVram_CanAliasIndexed(uint32_t psm) {
    return psm != 0x13u && psm != 0x1bu &&
           psm != 0x24u && psm != 0x2cu;
}
#endif
