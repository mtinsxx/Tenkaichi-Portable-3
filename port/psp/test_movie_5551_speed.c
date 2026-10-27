/* Bit-exact differential test for PSP MPEG YCbCr->RGB5551 optimization.
 * The reference loop is copied from the previously working PSP path.
 * No ISO, PSS or proprietary assets are required. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psp_movie_5551.h"

#define IW 512
#define IH 448
#define OW 480
#define OH 256
static uint8_t yplane[IW*IH], cb[IW*IH/4], cr[IW*IH/4], clip[1024];
static uint16_t candidate[IW*OH], reference[IW*OH];
static int32_t ty[256], trv[256], tgu[256], tgv[256], tbu[256];
static uint16_t xmap[OW], ymap[OH];
static uint32_t rng=0x81B29816u;
static uint32_t rnd(void) {
    rng^=rng<<13;
    rng^=rng>>17;
    rng^=rng<<5;
    return rng;
}
static void tables(void) {
    unsigned i;
    for (i=0;i<256u;i++) {
        ty[i]=76309*((int)i-16)+32768;
        trv[i]=104597*((int)i-128);
        tgu[i]=-25675*((int)i-128);
        tgv[i]=-53279*((int)i-128);
        tbu[i]=132201*((int)i-128);
    }
    for (i=0;i<1024u;i++)
        clip[i]=(uint8_t)(i<384u?0u:i>639u?255u:i-384u);
}
static void old_converter(void) {
    int x, y;
    for (y=0;y<OH;y++) {
        int sy=y*448/OH;
        const uint8_t *py=yplane+sy*IW;
        const uint8_t *pcb=cb+(sy>>1)*(IW>>1);
        const uint8_t *pcr=cr+(sy>>1)*(IW>>1);
        uint16_t *row=reference+y*IW;
        for (x=0;x<OW;x++) {
            int sx=x*512/OW, d=pcb[sx>>1], e=pcr[sx>>1];
            int c=ty[py[sx]];
            uint32_t red=clip[((c+trv[e])>>16)+384];
            uint32_t green=clip[((c+tgu[d]+tgv[e])>>16)+384];
            uint32_t blue=clip[((c+tbu[d])>>16)+384];
            row[x]=(uint16_t)(0x8000u | (red>>3) |
                         ((green>>3)<<5) | ((blue>>3)<<10));
        }
    }
}
int main(void) {
    unsigned pattern, x, i;
    tables();
    Psp_Movie5551InitMap(xmap,ymap);
    for (i=0;i<OW;i++)
        assert(xmap[i] == (uint16_t)(i*512u/480u));
    for (i=0;i<OH;i++)
        assert(ymap[i] == (uint16_t)(i*448u/256u));

    for (pattern=0;pattern<6u;pattern++) {
        for (i=0;i<sizeof(yplane);i++)
            yplane[i]=(uint8_t)(pattern==0?0:pattern==1?255:
                        pattern==2?128:pattern==3?i:pattern==4?i/512u:rnd());
        for (i=0;i<sizeof(cb);i++) {
            cb[i]=(uint8_t)(pattern==0?0:pattern==1?255:
                      pattern==2?128:pattern==3?i*13u:pattern==4?i/256u:rnd());
            cr[i]=(uint8_t)(pattern==0?255:pattern==1?0:
                      pattern==2?128:pattern==3?i*37u:pattern==4?i/4u:rnd());
        }
        for (i=0;i<IW*OH;i++) reference[i]=candidate[i]=0x1234u;
        old_converter();
        Psp_Movie5551Convert(candidate,IW,yplane,cb,cr,IW,xmap,ymap,
                             ty,trv,tgu,tgv,tbu,clip);
        if (memcmp(reference,candidate,sizeof(candidate))!=0) {
            for (i=0;i<IW*OH;i++)if(reference[i]!=candidate[i]) {
                fprintf(stderr,"movie pattern %u pixel %u ref %04X new %04X\n",
                        pattern,i,reference[i],candidate[i]);
                return 1;
            }
            assert(!"movie pixels differ");
        }
        for (i=0;i<OH;i++)
            for (x=OW;x<IW;x++)
                assert(candidate[i*IW+x] == 0x1234u);
    }
    puts("PSP MPEG direct RGB5551: 6 full frames bit-exact (including padded columns)");
    return 0;
}
