#ifndef BT3_PSP_TEX_LRU_H
#define BT3_PSP_TEX_LRU_H
#include <stdint.h>
/* The last GE frame is needed for eviction fencing. It is NOT a
 * sufficiently precise LRU key because hundreds of GS textures are
 * used within the same frame. Use a separate monotonically increasing
 * 32-bit access ticket for eviction priority, including wrap-around.
 * A valid LRU window cannot span >2^31 accesses. */
static inline int PspTexLru_IsOlder(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) < 0;
}
#endif
