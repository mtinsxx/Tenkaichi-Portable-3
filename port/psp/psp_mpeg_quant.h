#ifndef BT3_PSP_MPEG_QUANT_H
#define BT3_PSP_MPEG_QUANT_H
#include <stdint.h>
/* Precompute invariant MPEG-2 intra quantizer * qscale once per slice,
   rather than multiplying all AC VLC coefficients three times.
   A legal MPEG-2 qscale <=112; 2*255*112=57120 fits uint16_t.
   Exact 32-bit integer division/truncation is retained. */
static inline void PspMpegQuant_Build(uint16_t out[64],
                                     const uint8_t intra[64],
                                     unsigned qscale) {
    unsigned i;
    for(i=0;i<64u;i++)
        out[i]=(uint16_t)(2u*(unsigned)intra[i]*qscale);
}
static inline int PspMpegQuant_Apply(int level,unsigned factor) {
    return ((int)level*(int)factor)/32;
}
#endif
