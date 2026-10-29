#ifndef TP3_PSP_NATIVE_MESH_ASSETS_H
#define TP3_PSP_NATIVE_MESH_ASSETS_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* M3P1 / A3P1 are portable OFFLINE INTERMEDIATES, not a substitute
 * for BT3 proprietary bone/material/skin bindings. This reader is
 * independent of PSPSDK so the same bytes can be validated on host.
 * PSP is little-endian; inputs made by tools/psp_native_assets.py. */
enum { TP3_M3P_HEADER=16, TP3_M3P_VERTEX_BYTES=24,
       TP3_A3P_HEADER=16, TP3_A3P_MATRIX_BYTES=48,
       TP3_MAX_NATIVE_VERTICES=8192, TP3_MAX_NATIVE_INDICES=16384,
       TP3_MAX_NATIVE_BONES=32, TP3_MAX_NATIVE_FRAMES=128 };
typedef struct PspNativeMeshView {
    const uint8_t *vertices,*indices;
    uint32_t vertexCount,indexCount;
} PspNativeMeshView;
typedef struct PspNativeAnimView {
    const uint8_t *matrices;
    uint32_t boneCount,frameCount;
} PspNativeAnimView;

static inline uint32_t PspNativeAsset_U32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
           ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static inline uint16_t PspNativeAsset_U16(const uint8_t *p) {
    return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8));
}
static inline float PspNativeAsset_Float(const uint8_t *p) {
    uint32_t v=PspNativeAsset_U32(p);
    float result;
    memcpy(&result,&v,4);
    return result;
}
static inline int PspNativeMesh_Parse(const uint8_t *p,size_t n,
                                      PspNativeMeshView *out) {
    size_t vertsBytes,idxBytes;
    uint32_t v,k,i;
    if(!p||!out||n<TP3_M3P_HEADER||memcmp(p,"M3P1",4)||
       PspNativeAsset_U32(p+4)!=1u)return 0;
    v=PspNativeAsset_U32(p+8);k=PspNativeAsset_U32(p+12);
    if(!v||v>TP3_MAX_NATIVE_VERTICES||!k||k>TP3_MAX_NATIVE_INDICES||
       k%3u)return 0;
    vertsBytes=(size_t)v*TP3_M3P_VERTEX_BYTES;
    idxBytes=(size_t)k*2u;
    if(n!=TP3_M3P_HEADER+vertsBytes+idxBytes)return 0;
    for(i=0;i<v;i++) {
        const uint8_t *src=p+TP3_M3P_HEADER+(size_t)i*TP3_M3P_VERTEX_BYTES;
        unsigned a;
        for(a=0;a<5u;a++)
            if(!isfinite(PspNativeAsset_Float(src+a*4u)))return 0;
    }
    for(i=0;i<k;i++)
        if(PspNativeAsset_U16(p+TP3_M3P_HEADER+vertsBytes+i*2u)>=v)
            return 0;
    out->vertexCount=v;out->indexCount=k;
    out->vertices=p+TP3_M3P_HEADER;
    out->indices=p+TP3_M3P_HEADER+vertsBytes;
    return 1;
}
static inline int PspNativeAnim_Parse(const uint8_t *p,size_t n,
                                      PspNativeAnimView *out) {
    uint32_t b,f,i;
    size_t count;
    if(!p||!out||n<TP3_A3P_HEADER||memcmp(p,"A3P1",4)||
       PspNativeAsset_U32(p+4)!=1u)return 0;
    b=PspNativeAsset_U32(p+8);f=PspNativeAsset_U32(p+12);
    if(!b||b>TP3_MAX_NATIVE_BONES||!f||f>TP3_MAX_NATIVE_FRAMES)
        return 0;
    count=(size_t)b*f*12u;
    if(n!=TP3_A3P_HEADER+count*4u)return 0;
    for(i=0;i<count;i++)
        if(!isfinite(PspNativeAsset_Float(p+TP3_A3P_HEADER+i*4u)))
            return 0;
    out->boneCount=b;out->frameCount=f;out->matrices=p+TP3_A3P_HEADER;
    return 1;
}
static inline void PspNativeAnim_ApplyRigid(const PspNativeAnimView *anim,
        unsigned frame,unsigned bone,const float p[3],float out[3]) {
    const uint8_t *m;
    unsigned i;
    if(!anim || !p || !out || bone>=anim->boneCount) {
        if(out&&p)memcpy(out,p,3*sizeof(float));
        return;
    }
    frame %= anim->frameCount;
    m=anim->matrices+((size_t)frame*anim->boneCount+bone)*48u;
    for(i=0;i<3u;i++)
        out[i]=PspNativeAsset_Float(m+i*16u+0u)*p[0]+
               PspNativeAsset_Float(m+i*16u+4u)*p[1]+
               PspNativeAsset_Float(m+i*16u+8u)*p[2]+
               PspNativeAsset_Float(m+i*16u+12u);
}
#endif
