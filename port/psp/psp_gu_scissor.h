#ifndef TP3_PSP_GU_SCISSOR_H
#define TP3_PSP_GU_SCISSOR_H
#include <stdint.h>
/* PSP PSPSDK sceGuScissor(x,y,WIDTH,HEIGHT). The pre-existing
 * renderer passed right/bottom endpoints as WIDTH/HEIGHT, so
 * PS2 GS nonzero-origin scissor regions were enlarged on PSP.
 * GS SCISSOR_1/_2 endpoints are inclusive; sSt.scw/h already
 * contain (right-left+1). Convert to PSP pixel extents once.
 * Use uint64 arithmetic to keep calculations safe for 11-bit GS
 * positions and any malformed larger derived width/height. */
typedef struct PspGuClip { int x,y,w,h; } PspGuClip;
static inline PspGuClip PspGu_FromGs(int x,int y,int w,int h) {
    PspGuClip c={0,0,0,0};
    uint64_t x0,x1,y0,y1;
    if(x<0 || y<0 || w<=0 || h<=0)return c;
    x0=((uint64_t)(unsigned)x*480u)/512u;
    y0=((uint64_t)(unsigned)y*272u)/448u;
    x1=(((uint64_t)(unsigned)x+(unsigned)w)*480u+511u)/512u;
    y1=(((uint64_t)(unsigned)y+(unsigned)h)*272u+447u)/448u;
    if(x0>=480u || y0>=272u)return c;
    if(x1>480u)x1=480u;
    if(y1>272u)y1=272u;
    if(x1<=x0 || y1<=y0)return c;
    c.x=(int)x0; c.y=(int)y0;
    c.w=(int)(x1-x0);c.h=(int)(y1-y0);
    return c;
}
#endif
