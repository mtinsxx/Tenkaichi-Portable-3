#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_scene_pair.h"
int main(void) {
    /* 512x448/32: libgraph display 0 and 112 GS 8KiB pages. */
    assert(Psp_GsScenePairIsScreen(0, 0, 112, 1));
    assert(Psp_GsScenePairIsScreen(112, 0, 112, 1));
    assert(Psp_GsScenePairIsScreen(112, 112, 0, 1));
    assert(Psp_GsScenePairIsScreen(0, 112, 0, 1));
    assert(!Psp_GsScenePairIsScreen(112, 0, 112, 0));
    assert(!Psp_GsScenePairIsScreen(0, 112, 0, 0));
    assert(!Psp_GsScenePairIsScreen(224, 0, 112, 1)); /* Z/effects */
    assert(!Psp_GsScenePairIsScreen(56, 0, 112, 1));  /* work target */
    assert(!Psp_GsScenePairIsScreen(112, 0, -1, 1));
    assert(!Psp_GsScenePairIsScreen(112, 0, 0, 1));
    assert(Psp_GsScenePairIsScreen(13, -1, -1, 1));
    assert(Psp_GsScenePairIsScreen(56, 0, 56, 1)); /* smaller 16-bit pair */
    puts("BT3-PSP paired GS display FBPs and WORK isolation: passed");
    return 0;
}
