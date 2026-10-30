#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "gs_mirror_word_delta.h"

/* Same 32-bit storage word as the actual PS2 GS sVram[]: partial
 * planes are combined before comparing the old and new bytes.
 * This proves no-op detection cannot discard real byte changes. */
static uint32_t state=0x5421183du;
static uint32_t rnd(void) {
    state ^= state<<13;
    state ^= state>>17;
    state ^= state<<5;
    return state;
}
static uint32_t mask_for(unsigned psm,unsigned shift) {
    switch(psm) {
    case 0: return UINT32_MAX;            /* PSMCT32/PSMZ32 */
    case 1: return UINT32_C(0x00ffffff);  /* PSMCT24 */
    case 2: return UINT32_C(0xffff) << (shift*16u); /* 16/16S */
    case 3: return UINT32_C(0xff) << (shift*8u); /* 8-bit indexed */
    case 4: return UINT32_C(0xf) << (shift*4u); /* 4-bit indexed */
    case 5: return UINT32_C(0xff000000); /* PSMT8H */
    case 6: return UINT32_C(0x0f000000); /* PSMT4HL */
    default:return UINT32_C(0xf0000000); /* PSMT4HH */
    }
}
int main(void) {
    unsigned psm,i,changed=0,identical=0;
    assert(!GsMirror_WordChanged(UINT32_C(0xdeadbeef),
                                 UINT32_C(0xdeadbeef)));
    assert(GsMirror_WordChanged(UINT32_C(0xdeadbeef),
                                UINT32_C(0xdeadbeee)));
    for(psm=0;psm<8u;psm++) {
        for(i=0;i<150000u;i++) {
            unsigned shift=psm==2?rnd()%2u:psm==3?rnd()%4u:
                           psm==4?rnd()%8u:0u;
            uint32_t old=rnd(),candidate=rnd();
            uint32_t mask=mask_for(psm,shift);
            uint32_t next=GsMirror_MergeWord(old,candidate,mask);
            uint32_t reference=(old&~mask)|(candidate&mask);
            unsigned realChange=(old^next)!=0u;
            unsigned invalidations=0;
            /* Keep all other memory bits untouched. */
            assert(next==reference);
            assert((next&~mask)==(old&~mask));
            if(GsMirror_WordChanged(old,next))invalidations++;
            assert(invalidations==realChange);
            /* Repeating the same indexed/colour/depth write MUST NOT
             * cause another page generation bump or palette eviction. */
            assert(!GsMirror_WordChanged(next,
                      GsMirror_MergeWord(next,candidate,mask)));
            changed+=realChange;
            identical+=1u;
            /* A masked write containing exactly the old selected
             * bytes cannot alter a single bit, at any word offset. */
            assert(!GsMirror_WordChanged(old,
                      GsMirror_MergeWord(old,old,mask)));
        }
    }
    assert(changed>100000u && identical==1200000u);
    printf("exact GS dirty-word delta PASS: %u randomized subplane writes, %u changes, no false invalidations\n",
           identical,changed);
    return 0;
}
