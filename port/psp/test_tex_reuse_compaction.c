#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "psp_tex_reuse.h"
int main(void) {
    PspTexReuse e[PSP_TEX_REUSE_SLOTS]={0};
    uint32_t serial=1u, epoch=100u;
    unsigned removed=2u;
    Psp_TexReuseRemember(&e[0],1u,2u,epoch,serial,0,0,1);
    Psp_TexReuseRemember(&e[1],3u,4u,epoch,serial,0,0,removed);
    Psp_TexReuseRemember(&e[2],5u,6u,epoch,serial,0,0,5);
    Psp_TexReuseRemember(&e[3],7u,8u,epoch,serial-1u,0,0,4);
    assert(Psp_TexReuseOnErase(e,PSP_TEX_REUSE_SLOTS,removed,serial)==2u);
    assert(Psp_TexReuseMatches(&e[0],1u,2u,epoch,serial,0,0,5u));
    assert(!e[1].valid);
    assert(e[2].index==4u);
    assert(Psp_TexReuseMatches(&e[2],5u,6u,epoch,serial,0,0,5u));
    assert(e[3].index==4u);
    assert(!Psp_TexReuseMatches(&e[2],5u,6u,epoch+1u,serial,0,0,5u));
    assert(!Psp_TexReuseMatches(&e[2],5u,6u,epoch,serial,0,0,4u));
    assert(Psp_TexReuseOnErase(NULL,PSP_TEX_REUSE_SLOTS,0u,serial)==0u);
    puts("PSP texture cache descriptor compaction preserves unrelated guarded TEX0 keys");
    return 0;
}
