#include <assert.h>
#include <stdio.h>
#include "psp_log_all.h"
int main(void) {
    PspDiagAll d = {0};
    PspDiagAllSample s = {0};
    s.frame=100; s.fbp=112; s.zbp=400; s.tbp=112;
    s.ctx=1; s.framePsm=2; s.tex0=UINT64_C(0xabcdef0011223344);
    s.test=UINT64_C(0x12345678);
    s.prim=3; s.detail=12;
    PspDiagAll_Record(&d,PSP_DIAG_FB_INVALID_GENERATION,&s);
    assert(d.counts[PSP_DIAG_FB_INVALID_GENERATION]==1);
    assert(d.first[PSP_DIAG_FB_INVALID_GENERATION].frame==100);
    assert(d.first[PSP_DIAG_FB_INVALID_GENERATION].tex0==s.tex0);
    assert(d.first[PSP_DIAG_FB_INVALID_GENERATION].framePsm==2);
    s.frame=107;s.fbp=0;
    PspDiagAll_Record(&d,PSP_DIAG_FB_INVALID_GENERATION,&s);
    assert(d.counts[PSP_DIAG_FB_INVALID_GENERATION]==2);
    assert(d.first[PSP_DIAG_FB_INVALID_GENERATION].fbp==112);
    assert(d.lastFrame[PSP_DIAG_FB_INVALID_GENERATION]==107);
    PspDiagAll_Record(&d,PSP_DIAG_STAGE_INTERPRETER,&s);
    assert(d.counts[PSP_DIAG_STAGE_INTERPRETER]==1);
    assert(d.counts[PSP_DIAG_FB_UPLOAD_UNSUPPORTED]==0);
    PspDiagAll_Record(&d,PSP_DIAG_COUNT,&s);
    assert(d.counts[PSP_DIAG_STAGE_INTERPRETER]==1);
    PspDiagAll_Reset(&d);
    assert(d.counts[PSP_DIAG_STAGE_INTERPRETER]==0);
    assert(d.counts[PSP_DIAG_FB_INVALID_GENERATION]==0);
    assert(d.first[PSP_DIAG_FB_INVALID_GENERATION].frame==0);
    puts("PSP log_all bounded incident first-sample/last-frame passed");
    return 0;
}
