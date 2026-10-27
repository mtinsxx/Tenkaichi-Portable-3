#ifndef BT3_PSP_GPU_PROBE_H
#define BT3_PSP_GPU_PROBE_H
#include <stdint.h>
/* CPU probes of completed PSP GE framebuffer contents. Caller must
   first finish and sync GE, and pass uncached VRAM (0x44000000).
   On PPSSPP, GPU host framebuffer writes may still be deferred from
   emulated CPU memory; all-zero results are NOT proof of black GPU pixels.
   Sample the same spatial 16x12 grid of 480x272 5551 pixels from
   screen + two 512-stride WORK surfaces. This measures actual pixels,
   rather than only successful GS/GU draw submissions.
   It is diagnostic only: never changes color/depth/texture state. */
typedef struct PspGpuPixelProbe {
    uint32_t pixels;
    uint32_t colored;
    uint32_t alphaOnly;
    uint32_t hash;
} PspGpuPixelProbe;
static inline PspGpuPixelProbe PspGpuProbe5551(
       const volatile uint16_t *buffer, unsigned pitch,
       unsigned width, unsigned height) {
    PspGpuPixelProbe out={0,0,0,2166136261u};
    unsigned x,y;
    if (!buffer || pitch<width || !width || !height) return out;
    for (y=0;y<12u;y++) {
        const unsigned py=(y*(height-1u))/11u;
        for (x=0;x<16u;x++) {
            const unsigned px=(x*(width-1u))/15u;
            uint16_t v=buffer[py*pitch+px];
            ++out.pixels;
            if ((v&0x7FFFu)!=0u) ++out.colored;
            else if (v&0x8000u) ++out.alphaOnly;
            out.hash=(out.hash^(uint32_t)v)*16777619u;
        }
    }
    return out;
}
#endif
