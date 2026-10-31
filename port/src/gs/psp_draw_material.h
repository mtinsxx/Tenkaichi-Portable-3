#ifndef TP3_PSP_DRAW_MATERIAL_H
#define TP3_PSP_DRAW_MATERIAL_H
#include <stdint.h>
/* PSP-side, decoded immutable material for ONE GS draw.  The game still
 * supplies PS2 TEX0/CLAMP/PRIM metadata, but the PSP renderer no longer
 * decodes six bitfields and a 64-bit TEX0 for EVERY software feedback
 * texel.  The exact GS virtual VRAM stays the source of texture bytes.
 * No game-specific title ID, no external packs, no mode switch. */
typedef struct PspDrawMaterial {
    uint32_t tbp,tbw,psm,tw,th,cbp,cpsm;
    uint32_t tfx,tcc,textureEnabled,fst;
    uint32_t wms,wmt;
    int minu,maxu,minv,maxv;
} PspDrawMaterial;
static inline PspDrawMaterial PspDrawMaterial_Decode(uint64_t t0,
                                                      uint64_t cl,
                                                      uint64_t prim) {
    PspDrawMaterial m;
    m.tbp=(uint32_t)(t0&0x3fffu);
    m.tbw=(uint32_t)((t0>>14)&0x3fu);
    m.psm=(uint32_t)((t0>>20)&0x3fu);
    m.tw=1u<<((t0>>26)&15u);
    m.th=1u<<((t0>>30)&15u);
    m.cbp=(uint32_t)((t0>>37)&0x3fffu);
    m.cpsm=(uint32_t)((t0>>51)&15u);
    m.tfx=(uint32_t)((t0>>35)&3u);
    m.tcc=(uint32_t)((t0>>34)&1u);
    m.textureEnabled=(uint32_t)((prim>>4)&1u);
    m.fst=(uint32_t)((prim>>8)&1u);
    m.wms=(uint32_t)(cl&3u);
    m.wmt=(uint32_t)((cl>>2)&3u);
    m.minu=(int)((cl>>4)&0x3ffu);
    m.maxu=(int)((cl>>14)&0x3ffu);
    m.minv=(int)((cl>>24)&0x3ffu);
    m.maxv=(int)((cl>>34)&0x3ffu);
    return m;
}
static inline int PspDrawMaterial_Wrap(int value,unsigned size,
                                       unsigned mode,int lo,int hi) {
    if(mode==0u)return value&((int)size-1);
    if(mode==1u)return value<0?0:value>=(int)size?(int)size-1:value;
    if(mode==2u)return value<lo?lo:value>hi?hi:value;
    return (value&lo)|hi;
}
#endif
