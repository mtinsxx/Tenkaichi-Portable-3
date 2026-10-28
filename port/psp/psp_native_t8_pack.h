#ifndef TP3_PSP_NATIVE_T8_PACK_H
#define TP3_PSP_NATIVE_T8_PACK_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* Optional asset prepared from the user's own legally obtained decoded
 * PSMT8 indices. Source page-generation hash is REQUIRED: packs for
 * unrelated/modified GS texture contents are never accepted. An absent
 * pack changes nothing in the existing GS decoder. No copyrighted
 * assets or AFS/ISO data are kept in the repository. */
typedef struct PspNativeT8Header {
    uint8_t magic[4]; /* "T8P1" */
    uint32_t version, tbp, tbw, gen, width, height, dataFnv;
} PspNativeT8Header;
static inline uint32_t PspNativeT8_Fnv(const uint8_t *p,size_t n) {
    uint32_t h=2166136261u;
    size_t i;
    for(i=0;i<n;i++) h=(h^p[i])*16777619u;
    return h;
}
static inline int PspNativeT8_Match(const PspNativeT8Header *h,
         uint32_t tbp,uint32_t tbw,uint32_t gen,
         unsigned width,unsigned height) {
    const uint8_t magic[4]={'T','8','P','1'};
    return h && memcmp(h->magic,magic,4)==0 &&
        h->version==1u && h->tbp==tbp && h->tbw==tbw &&
        h->gen==gen && h->width==width && h->height==height &&
        width && height && width<=512u && height<=512u &&
        (size_t)width*height<=512u*512u;
}
#endif
