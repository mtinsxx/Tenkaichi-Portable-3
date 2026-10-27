/* PSP renderer uses affine UV after per-vertex ST/Q in both
   native-program and VU1-interpreted paths. Changing W across vertices
   is not by itself a reason to interpret an otherwise unclipped strip. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "psp_fighter_vu1.h"
static int nearf(float a, float b) { return fabsf(a-b)<1e-5f; }
static void eye(float *m) {
    unsigned i;
    memset(m,0,16*sizeof(float));
    for(i=0;i<4;i++)m[i*4+i]=1.0f;
}
int main(void) {
    float m[32], k[96], p[4]={0.2f,0.25f,0.3f,1.f};
    float rgba[4]={128.f,128.f,128.f,128.f};
    float st[4]={0.4f,0.7f,1.f,0.f}, normal[4]={0,0,1,1};
    PspStage4Vertex a,b;
    PspFighterVertex f1,f2;
    eye(m);eye(m+16);
    assert(PspStage4_Transform(&a,p,rgba,st,m,0,0));
    p[3]=2.f;
    assert(PspStage4_Transform(&b,p,rgba,st,m,0,0));
    assert(nearf(a.s/a.q,st[0]) && nearf(b.s/b.q,st[0]));
    assert(nearf(a.t/a.q,st[1]) && nearf(b.t/b.q,st[1]));
    assert(!nearf(a.q,b.q));
    memset(k,0,sizeof(k));
    eye(k);eye(k+16);eye(k+14*4);eye(k+18*4);
    p[3]=0.5f; /* fighter skin weight, NOT screen W */
    assert(PspFighter_Transform(&f1,p,normal,st,k,0));
    k[14*4+15]=2.f; k[18*4+15]=2.f;
    assert(PspFighter_Transform(&f2,p,normal,st,k,0));
    assert(!nearf(f1.q,f2.q));
    assert(nearf((f1.s0*f1.q)/f1.q,st[0]));
    assert(nearf((f2.s0*f2.q)/f2.q,st[0]));
    assert(nearf((f1.t0*f1.q)/f1.q,st[1]));
    assert(nearf((f2.t0*f2.q)/f2.q,st[1]));
    p[0]=5.f;
    assert(!PspStage4_Transform(&a,p,rgba,st,m,0,0));
    assert(!PspFighter_Transform(&f2,p,normal,st,k,0));
    puts("Native PSP varied-W affine ST/Q and clip fallback tests passed");
    return 0;
}
