#include <assert.h>
#include <stdio.h>
#include "psp_hud_overlay.h"
int main(void) {
    /* Both directions of a top-of-screen life/ki sprite qualify. */
    assert(Psp_GsIsBattleHudSprite(6,1,18,8,216,32));
    assert(Psp_GsIsBattleHudSprite(6,1,495,7,275,38));
    assert(Psp_GsIsBattleHudSprite(6,1,248,5,270,25)); /* clock */
    assert(!Psp_GsIsBattleHudSprite(3,1,10,6,250,20)); /* fighters/stage */
    assert(!Psp_GsIsBattleHudSprite(6,0,10,5,250,28)); /* offscreen FB */
    assert(!Psp_GsIsBattleHudSprite(6,1,40,130,480,280)); /* pause box */
    assert(!Psp_GsIsBattleHudSprite(6,1,0,20,512,448)); /* full-screen */
    assert(!Psp_GsIsBattleHudSprite(6,1,-300,10,20,40)); /* other overlay */
    assert(!Psp_GsIsBattleHudSprite(6,1,30,20,30,40)); /* zero width */
    puts("BT3-PSP top HUD conservative sprite classifier: passed");
    return 0;
}
