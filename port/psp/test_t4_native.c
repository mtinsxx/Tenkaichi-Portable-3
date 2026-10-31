#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psp_t4_native.h"
static uint32_t s=0x7252baf3u;
static uint32_t rnd(void){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void swizzle(uint8_t *dst,const uint8_t *src,unsigned rowBytes,
                    unsigned height) {
    unsigned y,x,row;
    for(y=0;y<height;y+=8u) for(x=0;x<rowBytes;x+=16u) {
        uint8_t *d=dst+((y/8u)*(rowBytes/16u)+x/16u)*128u;
        const uint8_t *p=src+y*rowBytes+x;
        for(row=0;row<8u;row++)memcpy(d+row*16u,p+row*rowBytes,16u);
    }
}
static void unswizzle(uint8_t *dst,const uint8_t *src,unsigned rowBytes,
                      unsigned height) {
    unsigned y,x,row;
    for(y=0;y<height;y+=8u)for(x=0;x<rowBytes;x+=16u) {
        const uint8_t *p=src+((y/8u)*(rowBytes/16u)+x/16u)*128u;
        uint8_t *d=dst+y*rowBytes+x;
        for(row=0;row<8u;row++)memcpy(d+row*rowBytes,p+row*16u,16u);
    }
}
int main(void) {
    unsigned w,h,i,rounds=0;
    assert(!PspT4_NativeEligible(0x24u,64u,64u,0));
    assert(!PspT4_NativeEligible(0x2cu,64u,64u,0));
    assert(!PspT4_NativeEligible(0x14u,16u,16u,0));
    assert(!PspT4_NativeEligible(0x14u,64u,4u,0));
    assert(!PspT4_NativeEligible(0x14u,64u,64u,1));
    for(w=32u;w<=512u;w*=2u)for(h=8u;h<=256u;h*=2u) {
        uint8_t original[512u*256u],linear[512u*256u];
        uint8_t tiled[512u*256u],unpacked[512u*256u];
        unsigned pixels=w*h;
        assert(PspT4_NativeEligible(0x14u,w,h,0));
        assert(PspT4_IndicesBytes(w,h)==pixels/2u);
        assert(PspT4_AllocatedBytes(w,h)>=pixels/2u+1024u);
        for(i=0u;i<pixels;i++)
            original[i]=(uint8_t)rnd();
        memcpy(linear,original,pixels);
        PspT4_PackInPlace(linear,pixels);
        for(i=0u;i<pixels;i++)
            assert(PspT4_Pixel(linear,i)==(original[i]&15u));
        memset(tiled,0,sizeof(tiled));
        memset(unpacked,0,sizeof(unpacked));
        swizzle(tiled,linear,w/2u,h);
        unswizzle(unpacked,tiled,w/2u,h);
        assert(memcmp(unpacked,linear,pixels/2u)==0);
        ++rounds;
    }
    printf("PSP native GU_T4 index packing/swizzle PASS: %u formats including 512x256, 4-bit CLUT, no packed high-plane alias\n",rounds);
    return 0;
}
