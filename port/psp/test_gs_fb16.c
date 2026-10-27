/* PSP GS: verify direct colour-target PSMCT16 and PSMCT16S layouts.
   GS VRAM layout and depth-buffer reinterpretation remain separate. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_gs_fb16.h"

int main(void) {
    unsigned p, c;
    for (p = 0; p < 64; ++p) {
        int permitted = p == 0x02u || p == 0x0Au;
        assert(Psp_GsCanRenderColor16(p, 100u, 200u, 1) == permitted);
        assert(Psp_GsCanRenderColor16(p, 100u, 200u, 0) == 0);
        assert(Psp_GsCanRenderColor16(p, 200u, 200u, 1) == 0);
    }
    assert(!Psp_GsCanRenderColor16(0x02u, 512u, 200u, 1));
    assert(!Psp_GsCanRenderColor16(0x02u, 100u, 512u, 1));
    assert(Psp_GsCanRenderColor16(0x0Au, 100u, 200u, 1));
    for (c = 0; c < 256; ++c) {
        uint16_t red = Psp_GsColor16((uint8_t)c, 0, 0, 0);
        uint16_t green = Psp_GsColor16(0, (uint8_t)c, 0, 0);
        uint16_t blue = Psp_GsColor16(0, 0, (uint8_t)c, 0);
        uint16_t alpha = Psp_GsColor16(0, 0, 0, (uint8_t)c);
        assert(red == (uint16_t)(c >> 3));
        assert(green == (uint16_t)((c >> 3) << 5));
        assert(blue == (uint16_t)((c >> 3) << 10));
        assert(alpha == (uint16_t)((c >= 128) << 15));
    }
    assert(Psp_GsColor16(255,255,255,255) == 0xFFFFu);
    assert(Psp_GsColor16(0,0,0,0) == 0u);
    puts("PSP native GS PSMCT16 and PSMCT16S direct colour output checks passed");
    return 0;
}
