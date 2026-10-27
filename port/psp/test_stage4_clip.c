/* Sutherland-Hodgman reference checks for PSP native stage VU1:
   clip, interpolated attributes, strip parity, and untouched fallback. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "psp_stage4_clip.h"

static void identity(float *m) {
    unsigned i;
    memset(m, 0, 16 * sizeof(float));
    for (i = 0; i < 4; ++i) m[i*4+i] = 1.0f;
}
static int near(float a, float b) { return fabsf(a-b) < 0.0002f; }
static PspStageClipVertex source(float x, float y, float z, float w) {
    PspStageClipVertex a;
    float pos[4] = {x,y,z,w},rgba[4]={128,64,32,255},st[4]={x,y,1,0};
    float m[16];
    identity(m);
    assert(PspStage4_ClipInput(&a,pos,rgba,st,m));
    return a;
}
int main(void) {
    float m[16], pos[4], color[4]={128,64,32,255}, st[4]={0.3f,0.8f,1,0};
    PspStageClipVertex tri[3], poly[PSP_STAGE_CLIP_MAX], v;
    PspStage4Vertex out;
    unsigned i;
    int n;
    identity(m);
    /* Stage strips are 12 floats per vertex: position, RGBA, ST.
       Every triangle must have a mathematically definite rejection
       plane; behind-eye triangles also qualify without division. */
    {
        float strip[4*12] = {0};
        unsigned k;
        for (k=0; k<4; ++k) {
            strip[k*12+3]=1.f;
            strip[k*12+4]=128.f;
            strip[k*12+7]=255.f;
        }
        strip[0]=-2.f; strip[12]=-2.5f; strip[24]=-3.f;
        assert(PspStage4_EntireStripOutside(strip,3,m));
        /* A second adjacent triangle on a different outside plane is
           also invisible: whole-strip proof is per-triangle. */
        strip[36]=-3.5f;
        assert(PspStage4_EntireStripOutside(strip,4,m));
        strip[24]=0.25f;
        assert(!PspStage4_EntireStripOutside(strip,4,m));
        /* One in-frustum vertex forbids discarding visible geometry. */
        strip[0]=0.f;
        assert(!PspStage4_EntireStripOutside(strip,3,m));
        /* PS2 CLIP compares +/-ABS(W), even for negative W.
           Behind-eye triangles can still pass the PS2 clip test:
           never cull them just because the sign is negative. */
        strip[0]=0.f; strip[12]=0.f; strip[24]=0.f;
        strip[3]=-1.f; strip[15]=-2.f; strip[27]=-3.f;
        assert(!PspStage4_EntireStripOutside(strip,3,m));
        /* All three are far outside the same +X plane even against
           ABS(W): safe to reject regardless of W sign. */
        strip[0]=5.f; strip[12]=6.f; strip[24]=7.f;
        assert(PspStage4_EntireStripOutside(strip,3,m));
        strip[24]=0.f; strip[27]=1.f; /* crossing W: not trivially outside */
        assert(!PspStage4_EntireStripOutside(strip,3,m));
        strip[0]=__builtin_nanf("");
        assert(!PspStage4_EntireStripOutside(strip,3,m));
        strip[0]=0.f;
        assert(!PspStage4_EntireStripOutside(strip,2,m));
        assert(!PspStage4_EntireStripOutside(strip,1025,m));
        /* Do not incorrectly discard a triangle whose corners lie
           outside three DIFFERENT planes but may surround the view. */
        strip[3]=strip[15]=strip[27]=1.f;
        strip[0]=-2.f; strip[1]=0.f;
        strip[12]=2.f; strip[13]=0.f;
        strip[24]=0.f; strip[25]=2.f;
        assert(!PspStage4_EntireStripOutside(strip,3,m));
    }
    tri[0]=source(-0.5f,0,0,1);
    tri[1]=source(0.5f,0,0,1);
    tri[2]=source(0,0.5f,0,1);
    assert(!tri[0].mask&&!tri[1].mask&&!tri[2].mask);
    assert(PspStage4_ClipTriangle(poly,tri)==3);
    tri[2]=source(2.f,0.5f,0,1);
    assert(tri[2].mask & (1u<<1));
    n=PspStage4_ClipTriangle(poly,tri);
    assert(n==4);
    for(i=0;i<(unsigned)n;++i) {
        unsigned pl;
        for(pl=0;pl<6;++pl)assert(PspStage4_ClipDistance(poly[i].clip,pl)>-0.00001f);
        assert(PspStage4_ClipProject(&out,&poly[i],m,0,0));
        assert(out.z==0u);
        assert(out.r==128u);
    }
    tri[0]=source(2.f,0,0,1);
    tri[1]=source(2.5f,0.2f,0,1);
    tri[2]=source(3.f,0.5f,0,1);
    assert(PspStage4_ClipTriangle(poly,tri)==0);
    tri[0]=source(0,0,0,1);
    tri[1]=source(0.5f,0,0,1);
    tri[2]=source(0,0.5f,0,1);
    n=PspStage4_ClipTriangle(poly,tri);
    assert(n==3);
    assert(PspStage4_ClipProject(&out,&poly[1],m,0.1f,0.2f));
    assert(near(out.x,0.4f) && near(out.y,-0.2f));
    pos[0]=0;pos[1]=0;pos[2]=0;pos[3]=0;
    assert(!PspStage4_ClipInput(&v,pos,color,st,m));
    pos[3]=-1;
    assert(!PspStage4_ClipInput(&v,pos,color,st,m));
    pos[3]=1;
    pos[0]=__builtin_nanf("");
    assert(!PspStage4_ClipInput(&v,pos,color,st,m));
    puts("PSP stage native six-plane clip, project and fallback tests passed");
    return 0;
}
