#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psp_vu1_predecode.h"
static uint32_t rng=0xDEC0DED1u;
static uint32_t rnd(void) {rng=rng*1664525u+1013904223u;return rng;}
static void check(PspVu1Instruction p,uint32_t lo,uint32_t hi){
  assert(p.lo==lo && p.hi==hi);
  assert(p.dest==((hi>>21)&15u) && p.ft==((hi>>16)&31u));
  assert(p.fs==((hi>>11)&31u) && p.fd==((hi>>6)&31u));
  assert(p.op==(hi&63u) && p.bc==(hi&3u));
  assert(p.lop==(lo>>25) && p.it==((lo>>16)&31u));
  assert(p.is==((lo>>11)&31u) && p.id==((lo>>6)&31u));
  assert(p.ld==((lo>>21)&15u));
  assert((int)p.imm11==((int32_t)(lo<<21)>>21));
  assert(p.imm15==(((lo>>10)&0x7800u)|(lo&0x7FFu)));
}
int main(void){
  uint8_t m[PSP_VU1_MICRO_BYTES]={0};
  PspVu1Instruction d[PSP_VU1_INSTRUCTION_COUNT]={0};
  unsigned n,k;
  assert(sizeof(PspVu1Instruction)<=24u);
  assert(PSP_VU1_INSTRUCTION_COUNT*sizeof(PspVu1Instruction)<=49152u);
  for(n=0;n<100000u;n++) {
    uint32_t lo=rnd(),hi=rnd();
    check(PspVu1_DecodeWords(lo,hi),lo,hi);
  }
  /* Arbitrarily ordered, half-word, and wrapping VIF uploads.
   * Compare every refreshed slot against the *actual* micro bytes. */
  for(n=0;n<12000u;n++) {
    unsigned addr=rnd()&0x7FFu;
    unsigned count=(rnd()&15u)+1u;
    for(k=0;k<count;k++) {
      uint32_t word=rnd();
      unsigned byte=PspVu1_UploadWordOffset(addr,k);
      uint32_t lo,hi;
      memcpy(m+byte,&word,4);
      PspVu1_RefreshInstruction(d,m,byte/8u);
      memcpy(&lo,m+(byte/8u)*8u,4);
      memcpy(&hi,m+(byte/8u)*8u+4u,4);
      check(d[byte/8u],lo,hi);
    }
  }
  assert(PspVu1_UploadWordOffset(2047u,0u)==0x3FF8u);
  assert(PspVu1_UploadWordOffset(2047u,1u)==0x3FFCu);
  assert(PspVu1_UploadWordOffset(2047u,2u)==0u);
  puts("PSP VU1 uploaded microcode predecode: 100k random opcode pairs + partial/wrapping MPG uploads PASS");
  return 0;
}
