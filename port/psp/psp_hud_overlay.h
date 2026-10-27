#ifndef BT3_PSP_HUD_OVERLAY_H
#define BT3_PSP_HUD_OVERLAY_H
/* PSP-only conservative classification for the life/ki/timer overlay.
   Works in original PS2 512x448 GS sprite coordinates (not PSP pixels).
   Do not extend to 3D triangles, work/effects targets or full-screen
   menus. The caller may disable depth writes/test for these sprites
   while retaining their original UVs, alpha, colour and draw order. */
static inline int Psp_GsIsBattleHudSprite(int type, int screen,
                                           float x0, float y0,
                                           float x1, float y1) {
    float xMin = x0 < x1 ? x0 : x1;
    float xMax = x0 > x1 ? x0 : x1;
    float yMin = y0 < y1 ? y0 : y1;
    float yMax = y0 > y1 ? y0 : y1;
    return type == 6 && screen &&
           /* Reject NaNs by comparing all bounds. */
           xMin >= -1.0f && xMax <= 513.0f &&
           yMin >= -1.0f && yMax <= 78.0f &&
           xMax > xMin && yMax > yMin;
}
#endif
