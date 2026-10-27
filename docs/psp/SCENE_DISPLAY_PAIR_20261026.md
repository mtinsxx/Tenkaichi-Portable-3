# PSP stage+fighter compositing: experimental display FBP routing

**Observation:** The user's PPSSPP recording shows some frames with full
stage/sky/ground and no fighters/HUD, alternating with frames showing
fighters/HUD and a black stage. This confirms stage resources can render.

**Hypothesis, not yet a proven visual repair:** PS2 libgraph provides two
alternating display-framebuffer addresses. Previous PSP backend routed
only `gGsMainFbp` to the physical screen, routing even the paired
display buffer to WORK along with unrelated effect targets. This branch
routes **both known display FBPs** from `sceGsSwapDBuff` to the active
PSP backbuffer. Other PS2 effect/aux FBPs remain in WORK.

Avoid unsafe texture feedback when two distinct logical FBPs now share
one real PSP screen: the existing GS VRAM-to-texture fallback replaces
sampling from the physical screen being written.

`scene_pair_screen=1` enables; `scene_pair_screen=0` restores prior
routing. `report=1` logs `GS scene paired-screen` with the two actual
libgraph FBPs and number of redirected primitives and frames.

**Unchanged:** `tex_limit=256`; original textures, colour/CLUT formats,
fighter VU1, animation, model geometry, lighting and stage VU1 transform.

**Required validation:** same battle with `scene_pair_screen=1` then 0,
normal and fast-forward; compare screenshots/FPS and logs. Black terrain
holes can also arise from scene clipping and incomplete offscreen effects;
this PR does not claim they are fixed. Risk: a game might intentionally
use the other display FBP as a previous-frame image, in which case
the full fix requires preserving both logical framebuffer surfaces
independently, not aliasing them. Do not merge without visual validation.
