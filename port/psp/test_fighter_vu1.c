/* PSP Allegrex fighter geometry differential/edge-case tests.
   Mathematical reference is independent of the production helper.
   Checks animated two-bone blends, pivots, cel-ramp lighting,
   camera-oriented fade lighting, screen/clip, invalid coordinates. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psp_fighter_vu1.h"

static int near(float a, float b) {
    return fabsf(a-b) <= 0.00001f;
}
static void identity(float *m) {
    unsigned i;
    memset(m, 0, sizeof(float)*16u);
    for (i=0;i<4;++i) m[i*4u+i]=1.0f;
}
static void constants(float *k) {
    memset(k,0,sizeof(float)*96u);
    identity(k);                /* bone A */
    identity(k+4*4u);          /* bone B */
    identity(k+14*4u);         /* screen */
    identity(k+18*4u);         /* clip */
    identity(k+10*4u);         /* camera/light for fade mode */
    k[22*4u]=220.0f; k[23*4u]=128.0f;
}
static void referenceBone(float out[3],const float *pos,
                          const float *k) {
    unsigned i,j;
    float a[3],b[3],w=pos[3];
    for (i=0;i<3;++i) {
        a[i]=k[3*4u+i];
        b[i]=k[7*4u+i];
        for (j=0;j<3;++j) {
            a[i]+=k[j*4u+i]*(pos[j]-k[8*4u+j]);
            b[i]+=k[(j+4)*4u+i]*(pos[j]-k[9*4u+j]);
        }
        out[i]=w*a[i]+(1.0f-w)*b[i];
    }
}
int main(void) {
    float k[96],pos[4]={0.20f,0.30f,0.40f,0.25f};
    float normal[4]={0.7f,0.3f,0.1f,1.0f},st[4]={0.35f,0.65f,1,0};
    PspFighterVertex out;
    float ref[3];
    unsigned i;
    constants(k);
    k[3*4u]=0.20f;
    k[7*4u]=-0.20f;
    assert(PspFighter_Transform(&out,pos,normal,st,k,0));
    referenceBone(ref,pos,k);
    assert(near(out.x,ref[0]) && near(out.y,ref[1]));
    assert(near(out.q,1.0f));
    assert(near(out.s0,0.35f) && near(out.t0,0.65f));
    /* Program 0: light vector from first row of the VU light matrix. */
    assert(near(out.s1,0.5f*(normal[0]+1.0f)));
    assert(near(out.t1,0));
    for (i=0;i<11;++i) {
        pos[3]=(float)i/10.0f;
        assert(PspFighter_Transform(&out,pos,normal,st,k,0));
        referenceBone(ref,pos,k);
        assert(near(out.x,ref[0]) && near(out.y,ref[1]));
    }
    /* Pivots affect the animated skin, not the lighting normal. */
    pos[3]=0.25f; k[8*4u]=0.05f; k[9*4u]=-0.03f;
    assert(PspFighter_Transform(&out,pos,normal,st,k,0));
    referenceBone(ref,pos,k);
    assert(near(out.x,ref[0]) && near(out.y,ref[1]));

    /* Program 1: normal in camera-aligned light frame. */
    assert(PspFighter_Transform(&out,pos,normal,st,k,1));
    assert(near(out.s1,0.85f) && near(out.t1,0.65f));
    /* Perspective with screen W=2 and clip W=2 should divide x/y. */
    k[14*4u+15u]=2.0f;
    k[18*4u+15u]=2.0f;
    assert(PspFighter_Transform(&out,pos,normal,st,k,1));
    referenceBone(ref,pos,k);
    assert(near(out.x,ref[0]/2.0f) && near(out.q,0.5f));
    k[14*4u+15u]=1.0f;
    k[18*4u+15u]=1.0f;

    /* The entire batch must use the original interpreter if any
       animated vertex is not safe to draw. */
    pos[3]=-0.01f; assert(!PspFighter_Transform(&out,pos,normal,st,k,0));
    pos[3]=1.01f;  assert(!PspFighter_Transform(&out,pos,normal,st,k,0));
    pos[3]=0.5f;
    pos[0]=2.5f; assert(!PspFighter_Transform(&out,pos,normal,st,k,0));
    pos[0]=0.2f;
    k[14*4u+15u]=0.0f;
    assert(!PspFighter_Transform(&out,pos,normal,st,k,0));
    k[14*4u+15u]=1.0f;
    normal[0]=__builtin_nanf("");
    assert(!PspFighter_Transform(&out,pos,normal,st,k,1));
    normal[0]=0.7f;
    assert(!PspFighter_Transform(NULL,pos,normal,st,k,0));
    puts("PSP native VU1 fighter bone animation / cel / fade tests passed");
    return 0;
}
