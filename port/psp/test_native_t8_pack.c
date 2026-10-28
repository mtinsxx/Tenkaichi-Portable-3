#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "psp_native_t8_pack.h"
int main(void) {
  const uint8_t v[]={0,1,2,3};
  PspNativeT8Header h={{'T','8','P','1'},1,336,8,0xa1b2c3d4u,2,2,0};
  assert(sizeof(h)==32u);
  assert(PspNativeT8_Fnv(v,4)==0xc3aa51b1u);
  h.dataFnv=PspNativeT8_Fnv(v,4);
  assert(PspNativeT8_Match(&h,336,8,0xa1b2c3d4u,2,2));
  assert(!PspNativeT8_Match(&h,336,8,0xa1b2c3d5u,2,2));
  assert(!PspNativeT8_Match(&h,368,8,0xa1b2c3d4u,2,2));
  assert(!PspNativeT8_Match(&h,336,16,0xa1b2c3d4u,2,2));
  assert(!PspNativeT8_Match(&h,336,8,0xa1b2c3d4u,512,0));
  assert(!PspNativeT8_Match(&h,336,8,0xa1b2c3d4u,513,2));
  h.magic[0]='X';
  assert(!PspNativeT8_Match(&h,336,8,0xa1b2c3d4u,2,2));
  puts("T8 native PSP cache pack header/gen/data FNV checks passed");
  return 0;
}
