#ifndef TP3_PSP_VU1_PREDECODE_H
#define TP3_PSP_VU1_PREDECODE_H
#include <stdint.h>
#include <string.h>

/* The PSP has no PS2 VU1 ISA. Decode the *immutable uploaded* microcode
 * into compact native MIPS-friendly fields only when VIF MPG changes a
 * program word. Executing the original VU1 op semantics remains required:
 * this is NOT a new shader, not a gameplay shortcut and not a bypass of
 * the interpreter's Q/CLIP latency, branches, or XGKICK. */
#define PSP_VU1_MICRO_BYTES 0x4000u
#define PSP_VU1_INSTRUCTION_COUNT (PSP_VU1_MICRO_BYTES / 8u)
typedef struct PspVu1Instruction {
    uint32_t lo,hi;
    uint8_t dest,ft,fs,fd,op,bc;
    uint8_t lop,it,is,id,ld;
    int16_t imm11;
    uint16_t imm15;
} PspVu1Instruction;

static inline PspVu1Instruction PspVu1_DecodeWords(uint32_t lo,uint32_t hi) {
    PspVu1Instruction o;
    o.lo=lo;
    o.hi=hi;
    o.dest=(uint8_t)((hi>>21)&15u);
    o.ft=(uint8_t)((hi>>16)&31u);
    o.fs=(uint8_t)((hi>>11)&31u);
    o.fd=(uint8_t)((hi>>6)&31u);
    o.op=(uint8_t)(hi&63u);
    o.bc=(uint8_t)(hi&3u);
    o.lop=(uint8_t)(lo>>25);
    o.it=(uint8_t)((lo>>16)&31u);
    o.is=(uint8_t)((lo>>11)&31u);
    o.id=(uint8_t)((lo>>6)&31u);
    o.ld=(uint8_t)((lo>>21)&15u);
    o.imm11=(int16_t)((int32_t)(lo<<21)>>21);
    o.imm15=(uint16_t)(((lo>>10)&0x7800u)|(lo&0x7FFu));
    return o;
}
/* Reading via memcpy handles 32-bit word alignment without aliasing
 * assumptions. Partial 32-bit uploads update only the matching half;
 * the other half stays as already loaded in VU micro RAM. */
static inline void PspVu1_RefreshInstruction(PspVu1Instruction *decoded,
                              const uint8_t *micro,unsigned instruction) {
    uint32_t lo,hi;
    unsigned pc=instruction & (PSP_VU1_INSTRUCTION_COUNT-1u);
    memcpy(&lo,micro+pc*8u,4);
    memcpy(&hi,micro+pc*8u+4u,4);
    decoded[pc]=PspVu1_DecodeWords(lo,hi);
}
static inline unsigned PspVu1_UploadWordOffset(unsigned programAddr,
                                              unsigned wordIndex) {
    return ((programAddr*8u+wordIndex*4u)&0x3FFCu);
}
#endif
