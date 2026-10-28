#ifndef TP3_PSP_TEXTURE_READ_VERIFY_H
#define TP3_PSP_TEXTURE_READ_VERIFY_H
#include <stdint.h>

/* Compare the native, block-decoded PSMT8 indices against the original
 * scalar GS VRAM reader at tile boundaries. This detects a bad fast
 * swizzle or out-of-date source without full-image verification. No GS
 * address is inferred from the physical PSP framebuffer.
 * Caller may reconstruct every index using reference reader on failure.
 */
typedef uint32_t (*PspT8ReadReference)(void *, unsigned, unsigned);
static inline int PspT8_CompareSamples(const uint8_t *decoded,
                   unsigned width, unsigned height, unsigned stride,
                   PspT8ReadReference ref, void *opaque,
                   unsigned *badX, unsigned *badY) {
    unsigned xs[8],ys[8],a,b;
    if(!decoded || !ref || !width || !height || stride<width) return 0;
    xs[0]=0u; xs[1]=7u; xs[2]=15u; xs[3]=16u;
    xs[4]=31u; xs[5]=width/2u; xs[6]=(width*3u)/4u; xs[7]=width-1u;
    ys[0]=0u; ys[1]=7u; ys[2]=15u; ys[3]=16u;
    ys[4]=31u; ys[5]=height/2u; ys[6]=(height*3u)/4u; ys[7]=height-1u;
    for(b=0;b<8u;b++) for(a=0;a<8u;a++) {
        unsigned x=xs[a]<width?xs[a]:width-1u;
        unsigned y=ys[b]<height?ys[b]:height-1u;
        if(decoded[y*stride+x]!=(uint8_t)ref(opaque,x,y)) {
            if(badX) *badX=x;
            if(badY) *badY=y;
            return 0;
        }
    }
    return 1;
}
#endif
