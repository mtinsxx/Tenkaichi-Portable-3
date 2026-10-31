#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_draw_material.h"

static uint32_t seed=0x13579bdfu;
static uint32_t next(void){
    seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;
}
static uint64_t next64(void){
    return ((uint64_t)next()<<32)|(uint64_t)next();
}
static int original_wrap(int v,int size,unsigned mode,int min,int max){
    if(mode==0u){v&=size-1;}
    else if(mode==1u){v=v<0?0:v>=size?size-1:v;}
    else if(mode==2u){v=v<min?min:v>max?max:v;}
    else{v=(v&min)|max;}
    return v;
}
int main(void) {
    unsigned n,k,checks=0;
    for(n=0;n<200000u;n++){
        uint64_t t0=next64(),cl=next64(),pr=next64();
        uint64_t test=next64(),al=next64(),zb=next64();
        uint64_t sc=next64(),fr=next64();
        unsigned depthBits;
        unsigned zp=0x30u|(unsigned)((zb>>24)&15u);
        PspDrawMaterial m=PspDrawMaterial_Decode(t0,cl,pr);
        PspDrawPixelState p;
        if((zp&15u)==0u)depthBits=32u;
        else if((zp&15u)==1u)depthBits=24u;
        else if((zp&15u)==2u || (zp&15u)==10u)depthBits=16u;
        else depthBits=32u;
        p=PspDrawPixelState_Decode(test,al,zb,pr,sc,fr,depthBits);
        assert(m.tbp==(t0&0x3fffu));
        assert(m.tbw==((t0>>14)&0x3fu));
        assert(m.psm==((t0>>20)&0x3fu));
        assert(m.tw==(1u<<((t0>>26)&15u)));
        assert(m.th==(1u<<((t0>>30)&15u)));
        assert(m.cbp==((t0>>37)&0x3fffu));
        assert(m.cpsm==((t0>>51)&15u));
        assert(m.tcc==((t0>>34)&1u));
        assert(m.tfx==((t0>>35)&3u));
        assert(m.textureEnabled==((pr>>4)&1u));
        assert(m.fst==((pr>>8)&1u));
        assert(m.wms==(cl&3u)&&m.wmt==((cl>>2)&3u));
        assert(m.minu==(int)((cl>>4)&0x3ffu));
        assert(m.maxu==(int)((cl>>14)&0x3ffu));
        assert(m.minv==(int)((cl>>24)&0x3ffu));
        assert(m.maxv==(int)((cl>>34)&0x3ffu));
        assert(p.test==test && p.alpha==al && p.zbuf==zb);
        assert(p.prim==pr&&p.scissor==sc&&p.frame==fr);
        assert(p.fbp==(uint32_t)(fr&0x1ffu)*32u);
        assert(p.fbw==(uint32_t)((fr>>16)&0x3fu));
        assert(p.fpsm==(uint32_t)((fr>>24)&0x3fu));
        assert(p.zbp==(uint32_t)(zb&0x1ffu)*32u);
        assert(p.zpsm==zp);
        assert(p.zmax==(depthBits==32u?UINT32_MAX:
                 depthBits==24u?0xffffffu:0xffffu));
        assert(p.writeZ==(((zb>>32)&1u)==0u));
        for(k=0;k<8;k++){
            int x=(int)(next()%131072u)-65536;
            int y=(int)(next()%131072u)-65536;
            assert(PspDrawMaterial_Wrap(x,m.tw,m.wms,m.minu,m.maxu)==
                   original_wrap(x,(int)m.tw,m.wms,m.minu,m.maxu));
            assert(PspDrawMaterial_Wrap(y,m.th,m.wmt,m.minv,m.maxv)==
                   original_wrap(y,(int)m.th,m.wmt,m.minv,m.maxv));
            checks+=2;
        }
    }
    printf("PSP native draw material parity PASS: 200000 random GS texture/frame/depth states and %u wrapped coordinates\n",checks);
    return 0;
}
