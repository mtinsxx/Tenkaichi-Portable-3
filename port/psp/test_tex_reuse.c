#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_tex_reuse.h"
int main(void) {
    PspTexReuse hot[8] = {0};
    const uint64_t t = UINT64_C(0xAABBCCDD00003012);
    const uint64_t a = UINT64_C(0x1234000000000080);
    unsigned s = Psp_TexReuseSlot(t, a, 256, 0);
    assert(s < 8u);
    assert(!Psp_TexReuseMatches(&hot[s], t, a, 7, 12, 256, 0, 4));
    Psp_TexReuseRemember(&hot[s], t, a, 7, 12, 256, 0, 3);
    assert(Psp_TexReuseMatches(&hot[s], t, a, 7, 12, 256, 0, 4));
    assert(!Psp_TexReuseMatches(&hot[s], t+1, a, 7, 12, 256, 0, 4));
    assert(!Psp_TexReuseMatches(&hot[s], t, a+1, 7, 12, 256, 0, 4));
    assert(!Psp_TexReuseMatches(&hot[s], t, a, 8, 12, 256, 0, 4));
    assert(!Psp_TexReuseMatches(&hot[s], t, a, 7, 13, 256, 0, 4));
    assert(!Psp_TexReuseMatches(&hot[s], t, a, 7, 12, 128, 0, 4));
    assert(!Psp_TexReuseMatches(&hot[s], t, a, 7, 12, 256, 1, 4));
    assert(!Psp_TexReuseMatches(&hot[s], t, a, 7, 12, 256, 0, 3));
    puts("PSP GS guarded texture reuse invalidation passed");
    return 0;
}
