#include <assert.h>
#include <stdio.h>
#include "psp_vu_profile.h"
int main(void) {
    assert(Psp_VuBucket(399)==PSP_VU_BUCKET_STAGE);
    assert(Psp_VuBucket(127)==PSP_VU_BUCKET_FIGHTER);
    assert(Psp_VuBucket(124)==PSP_VU_BUCKET_FADE);
    assert(Psp_VuBucket(451)==PSP_VU_BUCKET_EFFECT);
    assert(Psp_VuBucket(99)==PSP_VU_BUCKET_SHADOW);
    assert(Psp_VuBucket(101)==PSP_VU_BUCKET_SHADOW);
    assert(Psp_VuBucket(94)==PSP_VU_BUCKET_MISC_HLE);
    assert(Psp_VuBucket(104)==PSP_VU_BUCKET_MISC_HLE);
    assert(Psp_VuBucket(0)==PSP_VU_BUCKET_OTHER);
    assert(Psp_VuBucket(512)==PSP_VU_BUCKET_OTHER);
    puts("PSP VU1 runtime interpreter program attribution: passed");
    return 0;
}
