#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "psp_stage_clip_cache.h"
static PspStageClipCache cache;
static void identity(float *m) {
    int i;memset(m,0,16*sizeof(float));for(i=0;i<4;i++)m[i*4+i]=1.f;
}
static PspStageClipVertex input(float x,float y,float z,float w) {
    PspStageClipVertex o;float pos[4]={x,y,z,w},rgba[4]={255,128,64,200},
        st[4]={0.5f,0.25f,1.f,0.f},m[16];
    identity(m);assert(PspStage4_ClipInput(&o,pos,rgba,st,m));return o;
}
int main(void) {
    PspStageClipVertex tri[3],poly[PSP_STAGE_CLIP_MAX];
    PspStage4Vertex reference;
    float m[16];int n,k;identity(m);
    PspStageClipCache_Reset(&cache);
    tri[0]=input(-.5f,0,0,1);
    tri[1]=input(.5f,0,0,1);
    tri[2]=input(2.f,.5f,0,1);
    n=PspStage4_ClipTriangle(poly,tri);
    assert(n==4);
    assert(PspStageClipCache_Store(&cache,0,tri,m,.1f,.2f));
    assert(cache.used==4u&&cache.length[0]==4u);
    for(k=0;k<n;k++) {
        assert(PspStage4_ClipProject(&reference,&poly[k],m,.1f,.2f));
        assert(memcmp(&reference,&cache.projected[cache.begin[0]+k],
                      sizeof(reference))==0);
    }
    tri[0]=input(2.f,0,0,1);
    tri[1]=input(3.f,0,0,1);
    tri[2]=input(4.f,0,0,1);
    assert(PspStageClipCache_Store(&cache,1,tri,m,0,0));
    assert(cache.used==4u && cache.length[1]==0);
    tri[0]=input(-.5f,0,0,1);
    tri[1]=input(.5f,0,0,1);
    tri[2]=input(2.f,.5f,0,1);
    cache.used=PSP_STAGE_CLIP_CACHE_VERTICES-2u;
    assert(!PspStageClipCache_Store(&cache,2,tri,m,0,0));
    PspStageClipCache_Reset(&cache);
    assert(PspStageClipCache_Store(&cache,2,tri,m,0,0));
    assert(cache.used==4u);
    assert(!PspStageClipCache_Store(&cache,1024u,tri,m,0,0));
    puts("PSP VU1 stage399 clipped fan cache exact parity passed");
    return 0;
}
