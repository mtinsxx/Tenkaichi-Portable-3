#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_mpeg_quant.h"
int main(void) {
    uint8_t matrix[64];
    uint16_t factors[64];
    unsigned q,i,level;
    for(i=0;i<64;i++)matrix[i]=(uint8_t)(1u+i*4u);
    for(q=0;q<=112;q++) {
        PspMpegQuant_Build(factors,matrix,q);
        for(i=0;i<64;i++) {
            assert(factors[i]==2u*(unsigned)matrix[i]*q);
            for(level=1;level<=2047;level+=17) {
                int old=(int)(2u*level*(unsigned)matrix[i]*q)/32;
                assert(PspMpegQuant_Apply((int)level,factors[i])==old);
            }
        }
    }
    matrix[63]=255u;
    PspMpegQuant_Build(factors,matrix,112u);
    assert(factors[63]==57120u);
    assert(PspMpegQuant_Apply(2047,factors[63])==
           (int)(2u*2047u*255u*112u)/32);
    puts("PSP MPEG-2 quantizer precomputation: exact integer parity passed");
    return 0;
}
