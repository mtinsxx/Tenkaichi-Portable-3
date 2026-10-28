#ifndef TP3_PSP_IO_GUARD_H
#define TP3_PSP_IO_GUARD_H
#include <stddef.h>
#include <stdint.h>

/* A nonempty fread must have a valid stream and destination. Check at
 * our application's libc boundary, before entering newlib _fread_r.
 * Count*size must be representable as size_t, too. A null stream may
 * mean fopen failure OR memory corruption, never silently ignore it.
 * Empty reads are legal even with a null buffer. */
static inline int PspIo_BadFread(const void *dst, size_t size,
                                size_t count, const void *stream) {
    if (!size || !count) return 0;
    return !dst || !stream || size > SIZE_MAX / count;
}
#endif
