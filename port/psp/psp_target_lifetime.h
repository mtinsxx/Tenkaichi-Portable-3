#ifndef BT3_PSP_TARGET_LIFETIME_H
#define BT3_PSP_TARGET_LIFETIME_H
/* A logical GS framebuffer may change physical PSP backings after a swap. */
static inline int PspTarget_RebindStillRendered(int valid, const void *before, const void *after) {
    return valid && before == after;
}
/* GU clears the newly acquired screen buffer, not its WORK surface. */
static inline int PspTarget_KeepAfterScreenClear(int valid, const void *backing, const void *work) {
    return valid && backing == work;
}
#endif
