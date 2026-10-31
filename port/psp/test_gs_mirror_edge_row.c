#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gs_mirror_edge_row.h"
static uint32_t s=0x487210adu;
static uint32_t rnd(void){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static float reference(float ax,float ay,float bx,float by,float x,float y){
    return (bx-ax)*(y-ay)-(by-ay)*(x-ax);
}
static int identical(float a,float b){
    uint32_t x,y;memcpy(&x,&a,4);memcpy(&y,&b,4);return x==y;
}
int main(void){
    unsigned tri,y,x,checks=0;
    for(tri=0;tri<1000u;tri++){
        float ax=(float)((int)(rnd()%4096u)-2048)*0.25f;
        float ay=(float)((int)(rnd()%4096u)-2048)*0.25f;
        float bx=(float)((int)(rnd()%4096u)-2048)*0.25f;
        float by=(float)((int)(rnd()%4096u)-2048)*0.25f;
        for(y=0;y<16u;y++){
            float yf=(float)((int)(rnd()%1024u)-512);
            GsMirrorEdgeRow row=GsMirror_EdgeRow(ax,ay,bx,by,yf);
            for(x=0;x<128u;x++){
                float xf=(float)((int)(rnd()%1024u)-512);
                float src=reference(ax,ay,bx,by,xf,yf);
                float dst=GsMirror_EdgeAt(&row,xf);
                assert(identical(src,dst));
                ++checks;
            }
        }
    }
    printf("PSP GS edge-row exact parity PASS: %u reference bit-identical floating point values\n",checks);
    return 0;
}
