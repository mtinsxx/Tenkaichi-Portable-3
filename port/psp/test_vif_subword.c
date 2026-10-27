/* Host differential tests: compare PSP VIF1 Vn-8 / Vn-16 native
   conversion with the original generic PS2 VIF bit/component loop.
   Test covers wrap at VU 16 KiB, signed and unsigned extension,
   V1 replication, zero-fill lanes, WL<CL holes, and 256 vectors.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psp_vif_subword.h"

#define VUSIZE 0x4000u
static uint8_t nativeMem[VUSIZE], referenceMem[VUSIZE], data[4096];
static uint32_t rng = 0x91ABC635u;

static uint32_t random32(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* Independent reference following gs_vu1.c generic unmasked,
   mode=0 VIF unpack branch (including V1 broadcast). */
static void reference(uint8_t *mem, const uint8_t *src,
                      uint32_t addr, uint32_t count, uint32_t cl,
                      uint32_t wl, uint32_t vn, uint32_t vl,
                      uint32_t usn) {
    uint32_t k, c, cyc = 0, bitpos = 0;
    for (k = 0; k < count; k++) {
        uint32_t v[4] = {0, 0, 0, 0}, out[4];
        for (c = 0; c <= vn; c++) {
            uint32_t w = 0;
            uint32_t size = 32u >> vl;
            memcpy(&w, src + bitpos / 8, size / 8);
            if (!usn && size == 16) w = (uint32_t)(int32_t)(int16_t)w;
            if (!usn && size == 8) w = (uint32_t)(int32_t)(int8_t)w;
            v[c] = w;
            bitpos += size;
        }
        if (vn == 0u) v[1] = v[2] = v[3] = v[0];
        for (c = 0; c < 4; ++c) out[c] = v[c];
        memcpy(mem + ((addr & 0x3FFu) * 16u), out, 16);
        ++addr;
        if (++cyc == wl) {
            addr += cl - wl;
            cyc = 0;
        }
    }
}

static unsigned passed;

static void check(uint32_t vn, uint32_t vl, uint32_t usn,
                  uint32_t cl, uint32_t wl,
                  uint32_t addr, uint32_t count) {
    unsigned i;
    for (i = 0; i < VUSIZE; i++) {
        nativeMem[i] = (uint8_t)random32();
        referenceMem[i] = nativeMem[i];
    }
    for (i = 0; i < sizeof(data); i++) data[i] = (uint8_t)random32();
    Psp_VifUnpackSubword(nativeMem, data, addr, count, cl, wl,
                         vn, vl, usn);
    reference(referenceMem, data, addr, count, cl, wl, vn, vl, usn);
    if (memcmp(nativeMem, referenceMem, VUSIZE) != 0) {
        fprintf(stderr, "VIF mismatch: vn=%u vl=%u usn=%u CL=%u WL=%u "
                        "addr=%u count=%u\n",
                vn, vl, usn, cl, wl, addr, count);
        assert(!"PSP VIF subword diverged from PS2 reference");
    }
    passed++;
}

int main(void) {
    static const uint32_t addrs[] = {0u, 1u, 255u, 1022u, 1023u, 2047u};
    static const uint32_t counts[] = {1u, 2u, 3u, 7u, 16u, 31u, 256u};
    unsigned vl, vn, usn, cl, wl, a, n;

    for (vl = 1; vl <= 2; vl++)
        for (vn = 0; vn < 4; vn++)
            for (usn = 0; usn <= 1; usn++)
                for (cl = 1; cl <= 6; cl++)
                    for (wl = 1; wl <= cl; wl++)
                        for (a = 0; a < sizeof(addrs)/sizeof(addrs[0]); a++)
                            for (n = 0; n < sizeof(counts)/sizeof(counts[0]); n++)
                                check(vn,vl,usn,cl,wl,addrs[a],counts[n]);

    printf("PSP native VIF 8/16-bit: %u bit-exact reference comparisons passed\n",passed);
    return 0;
}
