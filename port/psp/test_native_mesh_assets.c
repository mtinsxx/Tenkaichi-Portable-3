#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psp_native_mesh_assets.h"

static void u32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static void f32(uint8_t *p,float v) {
    uint32_t b;
    memcpy(&b,&v,4);u32(p,b);
}
int main(void) {
    uint8_t mesh[16+3*24+3*2]={0};
    uint8_t anim[16+1*1*48]={0};
    PspNativeMeshView m={0};
    PspNativeAnimView a={0};
    float src[3]={1.f,2.f,3.f},dst[3];
    unsigned i;
    memcpy(mesh,"M3P1",4);u32(mesh+4,1);u32(mesh+8,3);u32(mesh+12,3);
    for(i=0;i<3;i++) {
        float x=(float)i;
        uint8_t *p=mesh+16+i*24;
        f32(p,x);f32(p+4,x+1);f32(p+8,x+2);
        f32(p+12,0.25f);f32(p+16,0.75f);
        p[20]=200;p[21]=100;p[22]=20;p[23]=255;
        mesh[16+3*24+2*i]=(uint8_t)i;
    }
    assert(PspNativeMesh_Parse(mesh,sizeof(mesh),&m));
    assert(m.vertexCount==3u && m.indexCount==3u);
    assert(PspNativeAsset_U16(m.indices+4)==2u);
    assert(!PspNativeMesh_Parse(mesh,sizeof(mesh)-1,&m));
    mesh[16+3*24+4]=5u;
    assert(!PspNativeMesh_Parse(mesh,sizeof(mesh),&m));
    mesh[16+3*24+4]=2u;
    f32(mesh+16,NAN);
    assert(!PspNativeMesh_Parse(mesh,sizeof(mesh),&m));
    f32(mesh+16,0.f);
    assert(PspNativeMesh_Parse(mesh,sizeof(mesh),&m));

    memcpy(anim,"A3P1",4);u32(anim+4,1);u32(anim+8,1);u32(anim+12,1);
    /* One row-major rigid 3x4 matrix: translation +3,-2,+4 */
    for(i=0;i<12;i++)f32(anim+16+i*4,0.f);
    f32(anim+16+0*4,1.f);f32(anim+16+3*4,3.f);
    f32(anim+16+5*4,1.f);f32(anim+16+7*4,-2.f);
    f32(anim+16+10*4,1.f);f32(anim+16+11*4,4.f);
    assert(PspNativeAnim_Parse(anim,sizeof(anim),&a));
    PspNativeAnim_ApplyRigid(&a,0,0,src,dst);
    assert(fabsf(dst[0]-4.f)<0.0001f);
    assert(fabsf(dst[1]-0.f)<0.0001f);
    assert(fabsf(dst[2]-7.f)<0.0001f);
    PspNativeAnim_ApplyRigid(&a,8,1,src,dst);
    assert(dst[0]==src[0] && dst[1]==src[1] && dst[2]==src[2]);
    assert(!PspNativeAnim_Parse(anim,sizeof(anim)-3,&a));
    puts("Native PSP mesh/animation binary validation and affine transform: OK");
    return 0;
}
