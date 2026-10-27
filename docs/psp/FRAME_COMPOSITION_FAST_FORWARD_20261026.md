# Flashing BT3 stage under PPSSPP fast-forward: frame-composition diagnosis

**Evidence:** In the user-supplied Oct 26 ~37-second PPSSPP screen recording,
stage-only images (sky/ground, no characters or battle HUD) alternate with
character/HUD-only images (black background). Stage textures and geometry
**can** render. The failure is more consistent with framebuffer routing,
frame boundaries, clearing, or compositing than with missing stage assets.
But those mechanisms are hypotheses, not yet individually confirmed.

## Current code
- \`gs_sony.c:sceGsSwapDBuff\` updates \`gGsMainFbp\` from the PS2 alternating FRAME draw environments.
- \`psp_gu.c:target_color\` maps that PS2 FBP to \`sMainDrawOffset\`, all other PS2 FBPs to a **single** WORK target.
- \`GsGpu_FrameEnd\` swaps PSP FB0/FB1 and immediately clears the next drawbuffer. The work surface may have another owner.
- PSP GU originally guessed the new render destination by flipping FB0/FB1. PSPSDK documents that \`sceGuSwapBuffers()\` **returns the new drawbuffer pointer**; compare and accept that authoritative value when it is one of the known buffers (relative, eDRAM mapped or uncached alias). If invalid, keep the old guess. \`swap_from_sdk=0\` disables the change.

## Verification
1. Use the same combat and leave all native VU1 and texture options unchanged. Configure \`report=1\` and first \`swap_from_sdk=1\`, then A/B \`swap_from_sdk=0\`. \`swap_vsync=1\` may independently reveal physical display-buffer timing; disable it again if no benefit.
2. New logs: \`GU swap verification\` tells whether the old parity assumption actually differed; \`frame composition\` samples the same completed PSP frame, listing \`GS-main-fbp\`, native-stage/fighter requests, screen/work draw counts and scratch-owner changes.
3. If stage-only frames contain \`stage-input>0, fighter-input=0\` and fighter-only frames the inverse, diagnose GS list/frame boundaries or emulation frame-skipping. If **both** are present in every frame, study late clear passes, depth/alpha, offscreen FBP composition and presentation ownership instead. The counters do not prove why a visible frame is black by themselves.

This is a diagnosis and swap correctness patch: no textures, geometry, layers,
fighter transforms, game files, or GS draw ordering are changed. It is NOT
legitimate to claim that the scenery or FPS has been fixed until the next PPSSPP
video/log confirms it.
