#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psp_shadow6.h"
int main(void) {
    float v[16]={0},u[17*4]={0};
    PspShadow6Vertex o;
    unsigned char valid[3]={1,1,1},flag[3]={0,0,0};
    u[0]=u[5]=u[10]=u[15]=1.0f; /* screen identity */
    u[48]=2.0f;u[53]=2.0f;u[64]=0.5f;
    v[0]=0.25f;v[1]=0.5f;v[2]=100.0f;
    /* z remains in GS units, w must be positive. */
    v[8]=128;v[9]=50;v[10]=33;v[11]=128;
    assert(PspShadow6_Transform(&o,v,u,0,0));
    assert(o.p.z==100u);
    assert(o.p.x>0.249f && o.p.x<0.251f);
    assert(o.p.y>0.499f && o.p.y<0.501f);
    assert(o.p.s>0.624f && o.p.s<0.626f);
    assert(o.p.t>0.749f && o.p.t<0.751f);
    assert(o.p.r==128&&o.p.g==50&&o.p.b==33&&o.p.a==128);
    assert(PspShadow6_TriangleVisible(valid,flag));
    flag[2]=1;
    assert(!PspShadow6_TriangleVisible(valid,flag));
    flag[2]=0;valid[1]=0;
    assert(!PspShadow6_TriangleVisible(valid,flag));
    valid[1]=1;
    {uint32_t one=1u;memcpy(&v[3],&one,sizeof(one));}
    assert(PspShadow6_Transform(&o,v,u,0,0)&&o.drop);
    v[0]=0.0f; u[15]=0.0f;
    assert(!PspShadow6_Transform(&o,v,u,0,0));
    /* Program6's clipped fans must use the second matrix (consts[16])
       to obtain clip-space XYZ/W and the third (consts[32]) for screen.
       A triangle crossing +X must produce a four-vertex polygon and
       preserve interpolated shadow ST and alpha; the old PSP path drew
       its unbounded three corners without clipping. */
    {
        float cv[3][16]={{0}}, fan[16]={0},pos[4]={0};
        PspStageClipVertex in[3],poly[PSP_STAGE_CLIP_MAX];
        PspStage4Vertex projected;
        unsigned n,j;
        int count;
        u[16]=u[21]=u[26]=u[31]=1.f;
        fan[0]=fan[5]=fan[10]=fan[15]=1.f;
        cv[0][0]=0.f;   cv[0][1]=-0.2f;
        cv[1][0]=0.5f;  cv[1][1]=0.2f;
        cv[2][0]=2.0f;  cv[2][1]=0.4f;
        for(n=0;n<3;n++){
            cv[n][8]=128.f;cv[n][9]=64.f;
            cv[n][10]=32.f;cv[n][11]=128.f;
            assert(PspShadow6_ClipInput(&in[n],cv[n],u));
        }
        assert((in[2].mask & (1u<<1))!=0u);
        count=PspStage4_ClipTriangle(poly,in);
        assert(count==4);
        for(j=0;j<(unsigned)count;j++){
            assert(PspStage4_ClipProject(&projected,&poly[j],fan,0,0));
            assert(projected.x>=-1.0001f && projected.x<=1.0001f);
            assert(projected.a==128);
        }
        /* The original flag is NOT the position's homogeneous W. */
        {uint32_t drop=1u;memcpy(&cv[1][3],&drop,sizeof(drop));}
        assert(PspShadow6_ClipInput(&in[1],cv[1],u));
        (void)pos;
    }
    puts("PSP program6 native ground-shadow geometry/UV/flags passed");
    return 0;
}
