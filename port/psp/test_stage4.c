/* Host-side arithmetic and fallback tests for the PSP's native
   VU1 stage program 4 transform. Does not simulate a full GS frame. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psp_stage4.h"

static int near(float a, float b) {
    float d = a - b;
    return d > -0.00001f && d < 0.00001f;
}

int main(void) {
    float m[32] = {0};
    float pos[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    float color[4] = {255.9f, 127.9f, -1.0f, 300.0f};
    float st[4] = {12.0f, 6.0f, 1.0f, 0.0f};
    float scratch[4];
    PspStage4Vertex out;
    unsigned i;
    for (i = 0; i < 4; ++i) {
        m[i * 4 + i] = 1.0f;
        m[16 + i * 4 + i] = 1.0f;
    }
    PspStage4_Multiply(scratch, m, pos);
    for (i = 0; i < 4; ++i)
        assert(near(scratch[i], pos[i]));
    assert(PspStage4_Transform(&out, pos, color, st, m, 0.1f, 0.2f));
    assert(near(out.x, 0.15f));
    assert(near(out.y, 0.3f));
    assert(out.z == 0u); /* cast truncates sub-pixel reference depth */
    assert(near(out.s, 12.0f) && near(out.t, 6.0f));
    assert(near(out.q, 1.0f));
    assert(out.r == 255u && out.g == 127u);
    assert(out.b == 0u && out.a == 255u);

    /* Non-unit W: perspective correction, not a constant assumption. */
    pos[3] = 2.0f;
    assert(PspStage4_Transform(&out, pos, color, st, m, 0.0f, 0.0f));
    assert(near(out.x, 0.125f));
    assert(near(out.s, 6.0f) && near(out.q, 0.5f));
    pos[3] = 1.0f;

    /* One coordinate outside clip: caller must keep original VU clipper. */
    pos[0] = 1.25f;
    assert(!PspStage4_Transform(&out, pos, color, st, m, 0, 0));
    pos[0] = 0.25f;

    /* Negative / zero W and non-finite matrix input must fallback. */
    pos[3] = 0.0f;
    assert(!PspStage4_Transform(&out, pos, color, st, m, 0, 0));
    pos[3] = -1.0f;
    assert(!PspStage4_Transform(&out, pos, color, st, m, 0, 0));
    pos[3] = 1.0f;
    m[0] = __builtin_nanf("");
    assert(!PspStage4_Transform(&out, pos, color, st, m, 0, 0));
    m[0] = 1.0f;

    /* 32-bit Z range guard and saturating RGBA conversion. */
    assert(PspStage4_Color(-20.0f) == 0);
    assert(PspStage4_Color(42.99f) == 42);
    assert(PspStage4_Color(999.0f) == 255);
    assert(PspStage4_Color(0.0f) == 0);
    puts("PSP native VU1 stage4 matrix/clip/color/fallback checks passed");
    return 0;
}
