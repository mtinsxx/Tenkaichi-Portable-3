#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psp_auto_model_prepare.h"

typedef struct Capture {
    unsigned seen,node,count;
    uint8_t first[3*48];
} Capture;
static void put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static int store(const uint8_t *v,unsigned count,unsigned node,void *user) {
    Capture *c=(Capture *)user;
    ++c->seen;c->node=node;c->count=count;
    memcpy(c->first,v,3u*48u);
    return 1;
}
int main(void){
    /* One loaded PS2 ObjMdlFile -> ObjMdlMesh, with the same 0x6C
       +0x60 linked resource structure as the real decomp. */
    uint8_t m[0x80+0x60+4+3*48+4+5*16+4]={0};
    size_t at=0x80+0x60;
    Capture c={0};
    unsigned i;
    put32(m+0x6c,0x80);
    m[0x80+6]=1; /* last */
    m[0x80+8]=1; /* enabled */
    m[0x80+10]=0x33; /* original fighter part node */
    put32(m+at,0x6c098005u);at+=4;
    for(i=0;i<3u;i++) {
        float vertex[12]={0};
        vertex[0]=(float)i;vertex[1]=1.0f;
        vertex[3]=0.5f;vertex[5]=1.0f;
        vertex[8]=0.25f;vertex[9]=0.75f;
        memcpy(m+at,vertex,48);at+=48;
    }
    put32(m+at,0x6c058000u);at+=4;
    memset(m+at,0x12,5u*16u);at+=5u*16u;
    put32(m+at,0x70000000u);at+=4;
    assert(PspAutoModel_Scan(m,sizeof(m),store,&c)==1u);
    assert(c.seen==1u && c.node==0x33u && c.count==3u);
    assert(PspAutoModel_F32(c.first+12)==0.5f);
    m[0x80+8]=0; /* disabled original submodel */
    assert(PspAutoModel_Scan(m,sizeof(m),store,&c)==0u);
    m[0x80+8]=1;
    put32(m+0x80+0x60,0x7c098005u); /* masked VIF not supported */
    assert(PspAutoModel_Scan(m,sizeof(m),store,&c)==0u);
    put32(m+0x80+0x60,0x6c098005u);
    m[0x80+0x60+4+12]=0xff; /* invalid 2-bone blend weight */
    m[0x80+0x60+4+13]=0xff;
    m[0x80+0x60+4+14]=0xff;
    m[0x80+0x60+4+15]=0xff;
    assert(PspAutoModel_Scan(m,sizeof(m),store,&c)==0u);
    assert(PspAutoModel_Scan(m,32u,store,&c)==0u);
    puts("Automatic original BT3 model prewarm: real VIF, materials skip, node and fallback PASS");
    return 0;
}
