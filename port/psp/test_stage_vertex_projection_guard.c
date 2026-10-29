/* Regression: unsafe VU1 stage projections must return to the original
 * interpreter BEFORE committing GS registers/triangles. Host C99 only;
 * real PPSSPP screenshots are still needed to validate visuals. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "psp_stage4_clip.h"

static void eye(float *m) {
    unsigned i;
    memset(m,0,16u*sizeof(float));
    for (i=0;i<4u;i++) m[i*4u+i]=1.0f;
}

int main(void) {
    float screen[16],clipMatrix[16],fan[16],pos[4]={0.5f,0.25f,0.0f,1.f};
    float clip[4],color[4]={128.f,128.f,128.f,255.f};
    float st[4]={.25f,.5f,1.f,0.f};
    PspStage4Vertex projected;
    PspStageClipVertex clipped;
    eye(screen); eye(clipMatrix); eye(fan);
    PspStage4_Multiply(clip,clipMatrix,pos);
    assert(PspStage4_ProjectionCoherent(pos,screen,clip,fan));
    assert(PspStage4_TransformClipReady(&projected,pos,color,st,
                                       screen,clip,0.f,0.f));
    assert(PspStage4_ClipInput(&clipped,pos,color,st,clipMatrix));
    assert(PspStage4_ClipProject(&projected,&clipped,fan,0.f,0.f));
    /* A homogeneous screen-matrix scale is harmless after W divide. */
    { unsigned i; for (i=0;i<16u;i++) screen[i]*=2.f; }
    assert(PspStage4_ProjectionCoherent(pos,screen,clip,fan));
    /* This mimics a clipped fan using a wrong matrix/camera epoch. */
    fan[12]=10.f;
    assert(!PspStage4_ProjectionCoherent(pos,screen,clip,fan));
    fan[12]=0.f;
    /* Different W, but compatible projections, are safe. */
    pos[3]=2.f;
    PspStage4_Multiply(clip,clipMatrix,pos);
    assert(PspStage4_ProjectionCoherent(pos,screen,clip,fan));
    /* A finite yet unrepresentable raw GS XYZ2 screen X must never
     * become a giant PSP floating-point triangle. */
    eye(screen); screen[12]=6000.f;
    assert(!PspStage4_TransformClipReady(&projected,pos,color,st,
                                         screen,clip,0.f,0.f));
    assert(!PspStage4_ProjectionCoherent(pos,screen,clip,fan));
    eye(fan); fan[12]=6000.f;
    assert(!PspStage4_ClipProject(&projected,&clipped,fan,0.f,0.f));
    assert(PspStage4_GsCoordRepresentable(-0.5f));
    assert(PspStage4_GsCoordRepresentable(4095.9375f));
    assert(!PspStage4_GsCoordRepresentable(4096.0f));
    assert(!PspStage4_GsCoordRepresentable(-4097.0f));
    assert(!PspStage4_GsCoordRepresentable(NAN));
    eye(screen); eye(fan);
    screen[0]=NAN;
    assert(!PspStage4_ProjectionCoherent(pos,screen,clip,fan));
    return 0;
}
