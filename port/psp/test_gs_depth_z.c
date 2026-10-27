/* Verify conversion used by GU_TRANSFORM_2D preserves GS Z order
   and never leaves the PSP's unsigned 16-bit depth range. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_gs_depth_z.h"

int main(void) {
    uint32_t z;
    assert(Psp_GsDepthZ24ToGuZ16(0u) == 0u);
    assert(Psp_GsDepthZ24ToGuZ16(0x000000FFu) == 0u);
    assert(Psp_GsDepthZ24ToGuZ16(0x00000100u) == 1u);
    assert(Psp_GsDepthZ24ToGuZ16(0x007FFFFFu) == 32767u);
    assert(Psp_GsDepthZ24ToGuZ16(0x00800000u) == 32768u);
    assert(Psp_GsDepthZ24ToGuZ16(0x00FFFFFFu) == 65535u);
    assert(Psp_GsDepthZ24ToGuZ16(0xFF800000u) == 32768u);
    for (z = 0; z < 0x00FFFFFFu; z += 257u) {
        assert(Psp_GsDepthZ24ToGuZ16(z) <= Psp_GsDepthZ24ToGuZ16(z + 1u));
        assert((float)Psp_GsDepthZ24ToGuZ16(z) == (float)((z & 0xFFFFFFu) >> 8));
    }
    puts("GS Z24 to PSP GU Z16 depth range tests passed");
    return 0;
}
