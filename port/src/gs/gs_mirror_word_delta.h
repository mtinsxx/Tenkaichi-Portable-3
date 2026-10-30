#ifndef TP3_GS_MIRROR_WORD_DELTA_H
#define TP3_GS_MIRROR_WORD_DELTA_H
#include <stdint.h>

/* PS2 virtual GS memory stores 32-bit words. 16/8/4-bit and masked
 * framebuffer writes can touch only a subplane. This is a *storage*
 * equality check on the final merged word, never a PSP RGB5551
 * colour comparison: destination alpha and indexed/plane feedback
 * remain byte-exact across all PS2 PSM aliases. */
static inline int GsMirror_WordChanged(uint32_t previous,
                                       uint32_t merged) {
    return previous != merged;
}
static inline uint32_t GsMirror_MergeWord(uint32_t previous,
                                          uint32_t value,
                                          uint32_t mask) {
    return (previous & ~mask) | (value & mask);
}
#endif
