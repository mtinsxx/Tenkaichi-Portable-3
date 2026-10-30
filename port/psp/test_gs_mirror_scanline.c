#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "gs_mirror_scanline.h"
typedef struct {float x,y;} V;
static uint32_t seed=0x3412fde6u;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static float edge(V a,V b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);}
static void check(V a,V b,V c,int l,int r,int y,unsigned *omitted,unsigned *accepted){
    float area=edge(a,b,c.x,c.y);
    int oldl=l,oldr=r,x;
    if(area==0.f)return;
    int valid=GsMirror_ClipScanline(a.x,a.y,b.x,b.y,c.x,c.y,area,y,&l,&r);
    if(valid){assert(l>=oldl);assert(r<=oldr);}
    for(x=oldl;x<=oldr;x++){
        float e0=edge(b,c,(float)x,(float)y),e1=edge(c,a,(float)x,(float)y);
        double w0=(double)e0/(double)area,w1=(double)e1/(double)area,w2=1.0-w0-w1;
        int covered=w0>=0.0 && w1>=0.0 && w2>=-1e-9;
        if(covered){(*accepted)++;assert(valid && x>=l && x<=r);}
        else if(!valid||x<l||x>r)(*omitted)++;
    }
}
int main(void){
    unsigned i,y,omitted=0,accepted=0;
    for(i=0;i<6000;i++){
        V a,b,c;
        int wide=(i%4==0),range=wide?4000:300;
        a.x=(float)((int)(rnd()%(unsigned)range)-range/2)/(wide?1.f:2.f);
        a.y=(float)((int)(rnd()%(unsigned)range)-range/2)/(wide?1.f:2.f);
        b.x=(float)((int)(rnd()%(unsigned)range)-range/2)/(wide?1.f:2.f);
        b.y=(float)((int)(rnd()%(unsigned)range)-range/2)/(wide?1.f:2.f);
        c.x=(float)((int)(rnd()%(unsigned)range)-range/2)/(wide?1.f:2.f);
        c.y=(float)((int)(rnd()%(unsigned)range)-range/2)/(wide?1.f:2.f);
        for(y=0;y<48;y++)check(a,b,c,0,95,(int)y,&omitted,&accepted);
    }
    {int l=0,r=95;assert(GsMirror_ClipScanline(0,0,10,0,0,10,NAN,0,&l,&r));assert(l==0&&r==95);}
    assert(omitted>100000 && accepted>1000);
    printf("GS mirror scanline clipping PASS: %u safely rejected pixels; %u retained coverage samples\n",omitted,accepted);
    return 0;
}
