#ifndef TP3_PSP_NATIVE_COLOR_PACK_H
#define TP3_PSP_NATIVE_COLOR_PACK_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Offline packed PSP GU color texels. Inputs must be the user's
 * legally extracted *GS-expanded* R,G,B,A bytes, not PNG gamma data.
 * The key includes the current GS VRAM page hash and TEXA; a pack
 * never silently replaces a different runtime texture or white-font
 * special case. The runtime additionally checks 17 actual GS pixels. */
enum { PSP_NATIVE_COLOR_5551=1u, PSP_NATIVE_COLOR_8888=2u };
typedef struct PspNativeColorHeader {
    uint8_t magic[4];             /* "N4P1" */
    uint32_t version,tbp,tbw,psm,gen,width,height;
    uint64_t texa;
    uint32_t whiteSprite,format,dataFnv;
} __attribute__((packed)) PspNativeColorHeader;
static inline uint32_t PspNativeColor_Fnv(const uint8_t *p,size_t n) {
    uint32_t h=2166136261u;
    size_t i;
    for(i=0;i<n;i++) h=(h^p[i])*16777619u;
    return h;
}
static inline unsigned PspNativeColor_Bytes(unsigned w,unsigned h,
                                            unsigned fmt) {
    if(!w || !h || w>512u || h>512u ||
       (fmt!=PSP_NATIVE_COLOR_5551 && fmt!=PSP_NATIVE_COLOR_8888))
        return 0u;
    return w*h*(fmt==PSP_NATIVE_COLOR_5551?2u:4u);
}
static inline int PspNativeColor_Match(const PspNativeColorHeader *h,
               uint32_t tbp,uint32_t tbw,uint32_t psm,uint32_t gen,
               uint64_t texa,unsigned w,unsigned height) {
    const uint8_t magic[4]={'N','4','P','1'};
    return h && sizeof(*h)==52u &&
        memcmp(h->magic,magic,4)==0 && h->version==1u &&
        h->tbp==tbp && h->tbw==tbw && h->psm==psm &&
        h->gen==gen && h->texa==texa &&
        h->width==w && h->height==height &&
        !h->whiteSprite && PspNativeColor_Bytes(w,height,h->format)!=0u;
}
static inline uint16_t PspNativeColor_Gu5551(uint32_t c) {
    return (uint16_t)(((c>>3)&31u)|(((c>>11)&31u)<<5)|
                      (((c>>19)&31u)<<10)|(((c>>24)!=0u)<<15));
}
static inline uint32_t PspNativeColor_Gu8888(uint32_t c) {
    unsigned a=(c>>24)&255u;
    a=a>=128u?255u:a*2u;
    return (c&0xffffffu)|((uint32_t)a<<24);
}
static inline int PspNativeColor_CheckPixel(const uint8_t *packed,
                             unsigned index,unsigned fmt,uint32_t gsColor) {
    if(fmt==PSP_NATIVE_COLOR_5551) {
        uint16_t c;
        memcpy(&c,packed+index*2u,2u);
        return c==PspNativeColor_Gu5551(gsColor);
    }
    if(fmt==PSP_NATIVE_COLOR_8888) {
        uint32_t c;
        memcpy(&c,packed+index*4u,4u);
        return c==PspNativeColor_Gu8888(gsColor);
    }
    return 0;
}
#endif
