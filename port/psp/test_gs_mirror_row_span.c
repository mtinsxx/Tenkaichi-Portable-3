#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "gs_mirror_row_span.h"
typedef struct V {float x,y;} V;
static uint32_t seed=0x65432fabu;
static uint32_t random_u32(void){
 seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;
}
static float edge(V a,V b,float x,float y){
 return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);
}
int main(void){
  unsigned caseId,skipped=0,covered=0;
  for(caseId=0;caseId<2000u;caseId++){
    V a,b,c;
    float area;
    int x,y;
    a.x=(float)((int)(random_u32()%401u)-200)/4.f;
    a.y=(float)((int)(random_u32()%401u)-200)/4.f;
    b.x=(float)((int)(random_u32()%401u)-200)/4.f;
    b.y=(float)((int)(random_u32()%401u)-200)/4.f;
    c.x=(float)((int)(random_u32()%401u)-200)/4.f;
    c.y=(float)((int)(random_u32()%401u)-200)/4.f;
    area=edge(a,b,c.x,c.y);
    if(area==0.f)continue;
    for(y=-32;y<32;y++){
      for(x=-32;x<32;x+=16){
        int right=x+15;
        int skip=GsMirror_RowSpanOutside(
            a.x,a.y,b.x,b.y,c.x,c.y,area,x,right,y);
        int xx;
        if(skip)skipped+=16u;
        for(xx=x;xx<=right;xx++){
            double w0=(double)edge(b,c,(float)xx,(float)y)/(double)area;
            double w1=(double)edge(c,a,(float)xx,(float)y)/(double)area;
            double w2=1.0-w0-w1;
            int reference=(w0>=0.0 && w1>=0.0 && w2>=-1e-9);
            if(reference)covered++;
            if(skip)assert(!reference);
        }
      }
    }
  }
  /* Extreme original stage vertex positions (offscreen geometry,
     screen-sized triangles and both windings). Allow large numeric
     endpoints but never clip a pixel that exact double math covers. */
  for(caseId=0;caseId<300u;caseId++){
    V a,b,c;
    float area;
    int x,y;
    a.x=(float)((int)(random_u32()%3401u)-1700);
    a.y=(float)((int)(random_u32()%3401u)-1700);
    b.x=(float)((int)(random_u32()%3401u)-1700);
    b.y=(float)((int)(random_u32()%3401u)-1700);
    c.x=(float)((int)(random_u32()%3401u)-1700);
    c.y=(float)((int)(random_u32()%3401u)-1700);
    area=edge(a,b,c.x,c.y);
    if(area==0.f)continue;
    for(y=0;y<64;y++)for(x=0;x<64;x+=16){
      int xx,skip=GsMirror_RowSpanOutside(
          a.x,a.y,b.x,b.y,c.x,c.y,area,x,x+15,y);
      for(xx=x;xx<x+16;xx++){
         double w0=(double)edge(b,c,(float)xx,(float)y)/(double)area;
         double w1=(double)edge(c,a,(float)xx,(float)y)/(double)area;
         double w2=1.0-w0-w1;
         if(skip)assert(!(w0>=0 && w1>=0 && w2>=-1e-9));
      }
    }
  }
  /* Critical cases: never cull an invalid or tiny-area triangle. */
  assert(!GsMirror_RowSpanOutside(0,0,100,0,0,100,NAN,0,15,0));
  assert(!GsMirror_RowSpanOutside(0,0,100,0,0,100,0.5f,0,15,0));
  assert(!GsMirror_RowSpanOutside(0,0,2500,0,0,100,100.f,0,15,0));
  assert(covered>0 && skipped>10000u);
  printf("GS mirror 16-pixel row-span exact inclusion: %u pixels skipped, %u covered across 2000 triangles PASS\n",
         skipped,covered);
  return 0;
}
