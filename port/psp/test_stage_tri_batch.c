/* Compare native fast GS triangle-packing with the normal PSP
   GsGpu_Draw packing equations, across FST, ST/Q and flat shading.
   Tests require no PS2 game files and do not depend on pspdev. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
typedef struct GsVertex {
    float x, y; uint32_t z; float s,t,q; int u,v;
    uint8_t r,g,b,a;
} GsVertex;
typedef struct Vtx {
    float u,v; uint32_t c; float x,y,z;
} Vtx;
#include "psp_gs_depth_z.h"
#include "psp_stage_tri_batch.h"
static int eq(float a, float b) {return fabsf(a-b) < .0001f;}
static unsigned gsAlpha(unsigned a) {return a>=128?255:a*2;}
static void reference(Vtx o[3], GsVertex v[3], uint64_t prim,
                       uint64_t t0, int tw, int th) {
    int fst=(prim>>8)&1, mod=((prim>>4)&1)&&(((t0>>35)&3)==0);
    int flat=!(prim&(1u<<3)); unsigned i;
    unsigned sw=1u<<((t0>>26)&15), sh=1u<<((t0>>30)&15);
    if (sw > 512u) sw = 512u;
    if (sh > 512u) sh = 512u;
    for(i=0;i<3;i++) {
        GsVertex *s=&v[i],*shade=flat?&v[2]:s;
        unsigned r=shade->r,g=shade->g,b=shade->b;
        float q=s->q? s->q:1.f;
        o[i].u=fst?(float)s->u/16.f*(tw?(float)tw/sw:1.f):
                       s->s/q*(tw?tw:1);
        o[i].v=fst?(float)s->v/16.f*(th?(float)th/sh:1.f):
                       s->t/q*(th?th:1);
        if(mod){r=gsAlpha(r);g=gsAlpha(g);b=gsAlpha(b);}
        o[i].c=r|(g<<8)|(b<<16)|(gsAlpha(shade->a)<<24);
        o[i].x=s->x*(480.f/512.f);
        o[i].y=s->y*(272.f/448.f);
        o[i].z=(float)Psp_GsDepthZ24ToGuZ16(s->z);
    }
}
int main(void) {
    GsVertex v[3]={0};Vtx a[3],b[3];
    const unsigned shades[3]={0,1,2};
    unsigned n,i,k;
    for(i=0;i<3;i++) {
        v[i].x=(float)(i*13+5);v[i].y=(float)(i*37+9);
        v[i].z=12345u+i*99;v[i].s=.25f*(i+1);
        v[i].t=.125f*(i+1);v[i].q=i?2.f:0.f;
        v[i].u=(int)(i*23+16);v[i].v=(int)(i*41+32);
        v[i].r=(uint8_t)(i*63);v[i].g=(uint8_t)(i*100);
        v[i].b=(uint8_t)(i*80);v[i].a=(uint8_t)(i*45);
    }
    for(n=0;n<2;n++)for(k=0;k<3;k++) {
        /* FST/non-FST, IIP/flat, MODULATE/REPLACE */
        uint64_t prim=(1ull<<4)|((uint64_t)n<<8)|
                      ((uint64_t)shades[k]<<3);
        uint64_t t0=(7ull<<26)|(6ull<<30)|
                    ((uint64_t)(k==2?1:0)<<35);
        PspStageTriScale c=PspStageTriMakeScale(prim,t0,128,64);
        PspStageTriPack(a,v,&c);
        reference(b,v,prim,t0,128,64);
        for(i=0;i<3;i++) {
            assert(a[i].c==b[i].c);
            assert(eq(a[i].u,b[i].u) && eq(a[i].v,b[i].v));
            assert(eq(a[i].x,b[i].x) && eq(a[i].y,b[i].y));
            assert(eq(a[i].z,b[i].z));
        }
    }
    puts("PSP native stage GS triangle reuse: full per-vertex parity OK");
    return 0;
}
