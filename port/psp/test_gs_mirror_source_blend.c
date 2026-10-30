#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "gs_mirror_fast.h"
static uint32_t ref(uint64_t alpha,uint32_t src,uint32_t dest,unsigned sa){
 unsigned a=alpha&3,b=(alpha>>2)&3,c=(alpha>>4)&3,d=(alpha>>6)&3;
 int aval=a==0?src:a==1?dest:0;
 int bval=b==0?src:b==1?dest:0;
 int dval=d==0?src:d==1?dest:0;
 int mult=c==0?sa:c==1?(dest>>24):(unsigned)(alpha>>32)&255u;
 int out=(((aval-bval)*mult)>>7)+dval;
 return out<0?0:out>255?255:(unsigned)out;
}
int main(void){
 uint32_t seed=0x54f72480u;
 unsigned i,optimized=0;
 for(i=0;i<200000u;i++){
     unsigned a,b,c,d,fix,sa,src,dst,chn;
     uint64_t alpha;
     seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
     a=seed&3u;b=(seed>>2)&3u;c=(seed>>4)&3u;
     d=(seed>>6)&3u;fix=(seed>>8)&255u;
     alpha=a|(uint64_t)b<<2|(uint64_t)c<<4|
           (uint64_t)d<<6|(uint64_t)fix<<32;
     sa=(seed>>16)&255u;
     src=seed&255u;dst=(seed>>20)&255u;
     if(GsMirror_BlendEqualsSource(alpha)){
         ++optimized;
         /* destination alpha is independently used as C=1, but the
            A==B or FIX=0 algebra guarantees it cancels regardless. */
         for(chn=0;chn<4u;chn++)
             assert(ref(alpha,src,(dst&255u)|((chn*85u)<<24),sa)==src);
     }
 }
 assert(optimized>2000u);
 /* C=FIX=0,D=SRC; source-equivalent independent of A and B. */
 assert(GsMirror_BlendEqualsSource((uint64_t)1u|
         (uint64_t)2u<<2|(uint64_t)2u<<4));
 /* D=destination is NOT a source-equivalent blend. */
 assert(!GsMirror_BlendEqualsSource(((uint64_t)1u<<6)|
         ((uint64_t)2u<<4)|((uint64_t)80u<<32)));
 printf("GS blend exact source-equivalence: %u/200000 true; all RGB and destination-alpha parity PASS\n",optimized);
 return 0;
}
