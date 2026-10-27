#ifndef TP3_PSP_GS_FB_RECONSTRUCT_H
#define TP3_PSP_GS_FB_RECONSTRUCT_H
#include <stdint.h>
/* First stage of PSP GS framebuffer virtualization.
 * Logical PS2 targets cannot all live in the 2 MiB GE VRAM.
 * Preserve completed 480x272 PSP colour targets in 512x512-pitch
 * main-RAM GE textures when the physical WORK target is reused.
 * These snapshots preserve 5551 pixels only; exact PSMCT32
 * destination-alpha/PSMT8 reinterpretation still needs the CPU GS
 * reference renderer. Treat this as a correct 16-bit staging lifetime,
 * NOT as byte-exact PS2 GS VRAM.
 *
 * GE transfer source/destination pitch is 512; width=480 height=272,
 * while texture size is POW2 512x512. Extra rows are initialized black.
 */
#define PSP_GS_FB_SNAPSHOT_W 512u
#define PSP_GS_FB_SNAPSHOT_H 512u
#define PSP_GS_FB_COPY_W 480u
#define PSP_GS_FB_COPY_H 272u
#define PSP_GS_FB_SNAPSHOT_BYTES (512u*512u*2u)
typedef struct PspGsFbVersion {
    uint32_t written;
    uint32_t saved;
    uint32_t sourceGen;
    unsigned frame;
    unsigned valid;
} PspGsFbVersion;
static inline void PspGsFb_OnDraw(PspGsFbVersion *v) {
    if(v) { ++v->written; if(!v->written) ++v->written; }
}
static inline int PspGsFb_CanSave(const PspGsFbVersion *v,
                                  unsigned frame) {
    return v && v->written != 0u &&
           (!v->valid || v->frame != frame);
}
static inline void PspGsFb_Saved(PspGsFbVersion *v,
                 unsigned frame,uint32_t generation) {
    if(!v)return;
    v->saved=v->written;
    v->sourceGen=generation;
    v->frame=frame;
    v->valid=1u;
}
static inline int PspGsFb_SampleValid(const PspGsFbVersion *v,
                                      uint32_t generation) {
    return v && v->valid && v->saved==v->written &&
           v->sourceGen==generation;
}
static inline void PspGsFb_Reset(PspGsFbVersion *v) {
    if(v) {v->written=0u;v->saved=0u;v->valid=0u;
           v->sourceGen=0u;v->frame=0u;}
}
/* GS source texture coordinates are in logical 512x448 screen space,
 * while the captured GE image is 480x272 pixels. For BOTH FST (texels)
 * and STQ (normalized then multiplied by TEX0.TW/TH), apply these
 * physical pixel scales. Previously the code divided by screen
 * dimensions (e.g. 512/480), stretching alias UVs the wrong way. */
static inline float PspGsFb_PhysicalU(float gsTexel) {
    return gsTexel*(480.f/512.f);
}
static inline float PspGsFb_PhysicalV(float gsTexel) {
    return gsTexel*(272.f/448.f);
}
#endif
