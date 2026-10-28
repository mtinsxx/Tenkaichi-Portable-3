#ifndef TP3_PSP_GS_SCREEN_COHERENCE_H
#define TP3_PSP_GS_SCREEN_COHERENCE_H
#include <stdint.h>

/* PSP screen images are physical 5551 surfaces, not identities of PS2
 * logical GS FBPs. A TEX0 lookup MUST NOT change a source framebuffer's
 * physical backing: only an actual FRAME write may migrate that backing.
 * The old source image must stay available until a GPU-ordered snapshot
 * is made, even if sceGuSwapBuffers has changed the draw offset. */
static inline int PspGsScreen_WritesTarget(int create) {
    return create != 0;
}
static inline int PspGsScreen_Slot(uintptr_t addr,uintptr_t fb0,uintptr_t fb1){
    if(addr==fb0)return 0;
    if(addr==fb1)return 1;
    return -1;
}
static inline int PspGsScreen_ChangesOwner(uint32_t previous,uint32_t next){
    return previous!=next;
}
/* A snapshot is a physical GE RGB5551 image. It may restore the new
 * drawbuffer only when it is byte-identical to the last logical FBP
 * version and no host-to-GS upload has changed that source page.
 * Do not restore directly to a WORK surface: its previous logical
 * owner must be captured by the existing work-pair code first. */
static inline int PspGsScreen_CanRestore(int screen,
                                         int alreadyRendered,
                                         int snapshotPresent,
                                         uint32_t savedWrites,
                                         uint32_t targetWrites,
                                         uint32_t savedGeneration,
                                         uint32_t currentGeneration){
    return screen && !alreadyRendered && snapshotPresent &&
           targetWrites!=0u && savedWrites==targetWrites &&
           savedGeneration==currentGeneration;
}
#endif
