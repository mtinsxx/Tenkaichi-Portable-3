#ifndef TP3_PSP_FB_FEEDBACK_PROVENANCE_H
#define TP3_PSP_FB_FEEDBACK_PROVENANCE_H
#include <stdint.h>
/* A GU framebuffer is not automatically represented in the emulated PS2
 * 4 MiB GS local memory. Cross-format GS texture reads (notably the
 * PSMCT32->PSMT8 palette-index view) require exact GS page reconstruction.
 * This predicate identifies that missing bridge; it does NOT claim that
 * RGB5551 can be losslessly converted into 32-bit GS render targets. */
static inline int PspFbFeedback_MissingCrossPsmReadback(
    int rendered,uint32_t sourceGeneration,uint32_t currentGeneration,
    uint32_t producerPsm,uint32_t texturePsm,int aliasCompatible) {
    (void)producerPsm; (void)texturePsm;
    return rendered && sourceGeneration==currentGeneration &&
           !aliasCompatible;
}
static inline int PspFbFeedback_IsIndexed(uint32_t psm) {
    return psm==0x13u || psm==0x14u || psm==0x1bu ||
           psm==0x24u || psm==0x2cu;
}
#endif
