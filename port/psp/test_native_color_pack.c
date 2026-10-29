#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psp_native_color_pack.h"
int main(void) {
    PspNativeColorHeader h;
    uint32_t rgb=0x807c4f19u;
    uint16_t word;
    uint32_t rgba;
    uint8_t payload[8];
    memset(&h,0,sizeof(h));
    assert(sizeof(h)==52u);
    assert(offsetof(PspNativeColorHeader,texa)==32u);
    memcpy(h.magic,"N4P1",4);
    h.version=1;
    h.tbp=336;
    h.tbw=8;
    h.psm=0;
    h.gen=0x12345678u;
    h.width=2;
    h.height=1;
    h.texa=0x1122334455667788ull;
    h.format=PSP_NATIVE_COLOR_5551;
    word=PspNativeColor_Gu5551(rgb);
    memcpy(payload,&word,2);
    assert(PspNativeColor_Match(&h,336,8,0,0x12345678u,
                                h.texa,2,1));
    assert(PspNativeColor_Bytes(2,1,PSP_NATIVE_COLOR_5551)==4u);
    assert(!PspNativeColor_Match(&h,336,8,0,0x12345679u,
                                h.texa,2,1));
    assert(!PspNativeColor_Match(&h,336,8,0,0x12345678u,
                                h.texa+1u,2,1));
    assert(!PspNativeColor_Match(&h,336,8,0,0x12345678u,
                                h.texa,1,1));
    assert(PspNativeColor_CheckPixel(payload,0,PSP_NATIVE_COLOR_5551,rgb));
    assert(!PspNativeColor_CheckPixel(payload,0,PSP_NATIVE_COLOR_5551,
                                      0x00000000u));
    h.format=PSP_NATIVE_COLOR_8888;
    rgba=PspNativeColor_Gu8888(0x40221f0du);
    assert(rgba==0x80221f0du);
    memcpy(payload,&rgba,4);
    assert(PspNativeColor_CheckPixel(payload,0,PSP_NATIVE_COLOR_8888,
                                     0x40221f0du));
    assert(!PspNativeColor_CheckPixel(payload,0,PSP_NATIVE_COLOR_8888,
                                      0x41221f0du));
    assert(PspNativeColor_Gu8888(0x81221f0du)==0xff221f0du);
    assert(PspNativeColor_Bytes(512,512,PSP_NATIVE_COLOR_8888)==1048576u);
    assert(PspNativeColor_Bytes(513,512,PSP_NATIVE_COLOR_8888)==0u);
    h.whiteSprite=1u;
    assert(!PspNativeColor_Match(&h,336,8,0,0x12345678u,
                                 h.texa,2,1));
    puts("Native PSP 5551/8888 offline pack key/format/samples tests passed");
    return 0;
}
