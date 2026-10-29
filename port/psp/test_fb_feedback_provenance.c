/* Missing GS VRAM feedback test: source is GPU-rendered in one format,
 * then sampled via an incompatible TEX0 interpretation. This is a
 * diagnostic, not a lossy RGB5551->PSMCT32 reconstruction. */
#include <assert.h>
#include "psp_fb_feedback_provenance.h"
int main(void) {
    assert(PspFbFeedback_MissingCrossPsmReadback(1,5,5,0x00,0x13,0));
    assert(PspFbFeedback_IsIndexed(0x13));
    assert(PspFbFeedback_IsIndexed(0x1b));
    assert(PspFbFeedback_IsIndexed(0x14));
    assert(!PspFbFeedback_IsIndexed(0));
    assert(!PspFbFeedback_MissingCrossPsmReadback(1,4,5,0,0x13,0));
    assert(!PspFbFeedback_MissingCrossPsmReadback(0,5,5,0,0x13,0));
    assert(!PspFbFeedback_MissingCrossPsmReadback(1,5,5,0,0,1));
    return 0;
}
