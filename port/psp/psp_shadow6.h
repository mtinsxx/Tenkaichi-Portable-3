#ifndef TP3_PSP_SHADOW6_H
#define TP3_PSP_SHADOW6_H
/* PS2 VU1 program6 ground-shadow geometry, ported from Vita's
   vu_transform(kind=2) to Allegrex C / PSP GU GS vertices.
   16 floats per input vertex: xyz, packed integer flags in word3,
   other vectors and RGBA at words8..11.
   Vita's kind2 screen matrix: u[10..13] = consts[0..15].
   Vita's shadow UV: u[0..3] = consts[12*4..16*4-1],
   scale u[8].x = consts[16*4].
   All triangles must preserve GS strip parity and flag-at-third-vertex.
   This path is not a substitute for actual shadow framebuffer compositing. */
#include "psp_stage4.h"
#include <stdint.h>
#include <string.h>
typedef struct PspShadow6Vertex {
    PspStage4Vertex p;
    unsigned drop;
} PspShadow6Vertex;
static inline int PspShadow6_Transform(PspShadow6Vertex *out,
                 const float *v, const float *consts, float ox,float oy) {
    float screen[4], q, z, u, t;
    uint32_t flag;
    unsigned i;
    if(!out || !v || !consts) return 0;
    memset(out,0,sizeof(*out));
    memcpy(&flag, v+3, sizeof(flag));
    out->drop=(flag & 65535u)!=0u;
    for(i=0;i<3u;i++)
        if(!__builtin_isfinite(v[i])) return 0;
    for(i=8u;i<12u;i++)
        if(!__builtin_isfinite(v[i])) return 0;
    PspStage4_Multiply(screen,consts,v);
    for(i=0;i<4u;i++)
        if(!__builtin_isfinite(screen[i]))return 0;
    if(!(screen[3]>0.0001f))return 0;
    q=1.0f/screen[3]; z=screen[2]*q;
    if(!__builtin_isfinite(z)||z<0.0f||z>16777215.0f)return 0;
    out->p.x=screen[0]*q-ox;
    out->p.y=screen[1]*q-oy;
    if(!__builtin_isfinite(out->p.x) || !__builtin_isfinite(out->p.y))
        return 0;
    out->p.z=(uint32_t)z;
    u=(consts[48u]*v[0]+consts[52u]*v[1]+
       consts[56u]*v[2]+consts[60u])*consts[64u];
    t=(consts[49u]*v[0]+consts[53u]*v[1]+
       consts[57u]*v[2]+consts[61u])*consts[64u];
    out->p.s=(u+1.0f)*0.5f;
    out->p.t=(t+1.0f)*0.5f;
    out->p.q=1.0f;
    if(!__builtin_isfinite(out->p.s) || !__builtin_isfinite(out->p.t))
        return 0;
    out->p.r=PspStage4_Color(v[8]);
    out->p.g=PspStage4_Color(v[9]);
    out->p.b=PspStage4_Color(v[10]);
    out->p.a=PspStage4_Color(v[11]);
    return 1;
}
/* The Vita/PS2 shader rejects a flagged THIRD strip vertex; malformed
   projected vertices must instead reject every triangle using them. */
static inline int PspShadow6_TriangleVisible(
       const unsigned char valid[3], const unsigned char flags[3]) {
    return valid[0]&&valid[1]&&valid[2]&&!flags[2];
}
#endif
