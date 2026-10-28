#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psp_texture_read_verify.h"
typedef struct {const uint8_t *p;unsigned stride;} Ref;
static uint32_t scalar(void *v,unsigned x,unsigned y){
   Ref *r=(Ref *)v;return r->p[y*r->stride+x];
}
int main(void){
    uint8_t data[64*64],decoded[64*64];
    Ref ref={data,64};
    unsigned i,x=999,y=999;
    for(i=0;i<64u*64u;i++)data[i]=(uint8_t)((i*31u+i/64u)&255u);
    memcpy(decoded,data,sizeof(data));
    assert(PspT8_CompareSamples(decoded,64,64,64,scalar,&ref,&x,&y));
    decoded[16*64+15]^=1;
    assert(!PspT8_CompareSamples(decoded,64,64,64,scalar,&ref,&x,&y));
    assert(x==15u && y==16u);
    memcpy(decoded,data,sizeof(data));
    decoded[63*64+63]^=255u;
    assert(!PspT8_CompareSamples(decoded,64,64,64,scalar,&ref,NULL,NULL));
    assert(!PspT8_CompareSamples(decoded,64,64,7,scalar,&ref,NULL,NULL));
    assert(!PspT8_CompareSamples(NULL,64,64,64,scalar,&ref,NULL,NULL));
    assert(!PspT8_CompareSamples(decoded,64,64,64,NULL,&ref,NULL,NULL));
    assert(!PspT8_CompareSamples(decoded,0,64,64,scalar,&ref,NULL,NULL));
    puts("PSP PSMT8 reference comparison catches tile-boundary corruption");
    return 0;
}
