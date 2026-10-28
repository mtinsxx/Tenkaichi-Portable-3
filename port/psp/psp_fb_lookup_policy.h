#ifndef TP3_PSP_FB_LOOKUP_POLICY_H
#define TP3_PSP_FB_LOOKUP_POLICY_H
/* Reading GS TEX0 may never change the framebuffer physical owner.
 * Render writes still use target_color's current screen backing. */
static inline int PspFbLookup_CanChangeBacking(int create) {
    return create!=0;
}
#endif
