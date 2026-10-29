#include <pspgu.h>
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "psp_native_mesh_assets.h"
#include "psp_native_mesh_preview.h"

extern void Psp_Log(const char *, ...);
typedef struct PspNativePreviewVtx {
    float u,v;
    uint32_t color;
    float x,y,z;
} PspNativePreviewVtx;
static uint8_t *sMeshData,*sAnimData;
static PspNativeMeshView sMesh;
static PspNativeAnimView sAnim;
static PspNativePreviewVtx *sOutput;
static void *sOutputRaw;
static unsigned sDraws;
static int sEnabled;

static uint8_t *load_small(const char *path,size_t capacity,size_t *length) {
    FILE *f=fopen(path,"rb");
    uint8_t *p;
    long n;
    if(!f)return NULL;
    if(fseek(f,0,SEEK_END)!=0 || (n=ftell(f))<=0 ||
       (size_t)n>capacity || fseek(f,0,SEEK_SET)!=0) {
        fclose(f);return NULL;
    }
    p=(uint8_t *)malloc((size_t)n);
    if(!p){fclose(f);return NULL;}
    if(fread(p,1,(size_t)n,f)!=(size_t)n) {
        free(p);fclose(f);return NULL;
    }
    fclose(f);
    *length=(size_t)n;
    return p;
}
void PspNativePreview_Init(void) {
    size_t n=0;
    FILE *flag=fopen("ms0:/PSP/GAME/BT3/native/enable-preview.txt","rb");
    if(!flag) return; /* The real game stays completely untouched. */
    fclose(flag);
    sMeshData=load_small("ms0:/PSP/GAME/BT3/native/preview.m3p",
                         16u+TP3_MAX_NATIVE_VERTICES*24u+
                         TP3_MAX_NATIVE_INDICES*2u,&n);
    if(!sMeshData || !PspNativeMesh_Parse(sMeshData,n,&sMesh)) {
        Psp_Log("psp: native preview: invalid/missing M3P1 (disabled)\n");
        free(sMeshData);sMeshData=NULL;
        return;
    }
    if(sMesh.indexCount>4096u) {
        Psp_Log("psp: native preview: too many vertices in optional preview\n");
        free(sMeshData);sMeshData=NULL;return;
    }
    n=0;
    sAnimData=load_small("ms0:/PSP/GAME/BT3/native/preview.a3p",
                         16u+TP3_MAX_NATIVE_BONES*
                         TP3_MAX_NATIVE_FRAMES*48u,&n);
    if(sAnimData && !PspNativeAnim_Parse(sAnimData,n,&sAnim)) {
        Psp_Log("psp: native preview: invalid animation, using static mesh\n");
        free(sAnimData);sAnimData=NULL;
    }
    sOutputRaw=malloc((size_t)sMesh.indexCount*sizeof(*sOutput)+63u);
    if(!sOutputRaw){
        free(sAnimData);free(sMeshData);
        sAnimData=sMeshData=NULL;
        return;
    }
    sOutput=(PspNativePreviewVtx *)
          (((uintptr_t)sOutputRaw+63u)&~(uintptr_t)63u);
    sEnabled=1;
    Psp_Log("psp: native PSP GU preview enabled: mesh vertices=%u triangles=%u animation frames=%u bones=%u (independent diagnostic, not BT3 model replacement)\n",
            sMesh.vertexCount,sMesh.indexCount/3u,
            sAnimData?sAnim.frameCount:0u,
            sAnimData?sAnim.boneCount:0u);
}
void PspNativePreview_Draw(unsigned frame) {
    unsigned i;
    if(!sEnabled || !sMeshData || !sOutput)return;
    for(i=0;i<sMesh.indexCount;i++) {
        unsigned j=PspNativeAsset_U16(sMesh.indices+i*2u);
        const uint8_t *src=sMesh.vertices+(size_t)j*24u;
        PspNativePreviewVtx *v=&sOutput[i];
        float p[3],transformed[3];
        unsigned k;
        for(k=0;k<3u;k++) p[k]=PspNativeAsset_Float(src+k*4u);
        if(sAnimData)
            PspNativeAnim_ApplyRigid(&sAnim,frame/2u,0u,p,transformed);
        else memcpy(transformed,p,sizeof(p));
        /* Compact overlay area; positions are normalized [-1,+1].
         * Offscreen/clipped geometry is intentionally not used to replace
         * any real GS primitive. This is only an opt-in loader test. */
        v->x=390.0f+transformed[0]*60.0f;
        v->y=72.0f-transformed[1]*60.0f;
        v->z=0.0f;
        v->u=PspNativeAsset_Float(src+12);
        v->v=PspNativeAsset_Float(src+16);
        v->color=PspNativeAsset_U32(src+20);
    }
    sceKernelDcacheWritebackRange((void *)sOutput,
                         sMesh.indexCount*sizeof(*sOutput));
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuScissor(0,0,480,272);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuShadeModel(GU_SMOOTH);
    sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|
                 GU_VERTEX_32BITF|GU_TRANSFORM_2D,
                 (int)sMesh.indexCount,0,sOutput);
    ++sDraws;
    if(sDraws==1u)
        Psp_Log("psp: native PSP GU preview: first converted mesh submitted directly to GE, triangles=%u; original BT3 meshes still use original renderer\n",
                sMesh.indexCount/3u);
}
