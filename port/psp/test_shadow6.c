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
    puts("PSP program6 native ground-shadow geometry/UV/flags passed");
    return 0;
}
