#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_gpu_probe.h"
static uint16_t pixels[512u*272u];
int main(void) {
    PspGpuPixelProbe p;
    p=PspGpuProbe5551(pixels,512,480,272);
    assert(p.pixels==192u && p.colored==0u && p.alphaOnly==0u);
    pixels[0]=0x8000u;
    p=PspGpuProbe5551(pixels,512,480,272);
    assert(p.alphaOnly==1u && p.colored==0u);
    pixels[(272u-1u)*512u+479u]=0x7FFFu;
    p=PspGpuProbe5551(pixels,512,480,272);
    assert(p.colored==1u && p.alphaOnly==1u);
    assert(p.hash!=0u);
    assert(PspGpuProbe5551(pixels,128,480,272).pixels==0u);
    puts("PSP GE-completed screen/work surface probe passed");
    return 0;
}
