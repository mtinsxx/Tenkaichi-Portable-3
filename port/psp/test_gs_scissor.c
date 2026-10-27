#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "psp_gs_scissor.h"
static uint64_t rect(unsigned a,unsigned b,unsigned c,unsigned d) {
    return (uint64_t)a|((uint64_t)b<<16)|((uint64_t)c<<32)|((uint64_t)d<<48);
}
int main(void){
    const float in[6]={100,100,200,200,150,200};
    const float out[6]={550,100,650,200,600,200};
    const float partly[6]={-5,15,100,100,-20,200};
    const float invalid[6]={NAN,0,200,200,150,150};
    assert(!PspGs_ScissorReject(rect(0,511,0,447),in,3));
    assert(PspGs_ScissorReject(rect(0,511,0,447),out,3));
    assert(!PspGs_ScissorReject(rect(0,511,0,447),partly,3));
    assert(!PspGs_ScissorReject(rect(0,511,0,447),invalid,3));
    assert(!PspGs_ScissorReject(rect(101,101,100,100),in,3));
    assert(PspGs_ScissorReject(rect(130,129,0,447),in,3));
    assert(PspGs_ScissorViewportEmpty(rect(130,129,0,447)));
    assert(PspGs_ScissorViewportEmpty(rect(0,511,0,0))==0);
    assert(PspGs_ScissorViewportEmpty(rect(800,900,0,447)));
    puts("PS2 GS inclusive scissor, empty clips and conservative early-cull passed");
    return 0;
}
