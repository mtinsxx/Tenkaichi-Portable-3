# port/psp

The PSP version of the port. Everything in this folder exists only for the PSP; the game's code and the rest of
`port/` are shared with the PC builds, and most of the PSP's changes to them are inside `#ifdef PORT_PSP` (or
inside `#ifdef PORT_CONSOLE`, next to the Vita's).

- Building: [docs/psp/building.md](../../docs/psp/building.md)
- The development build, its settings and a map of the files here: [docs/psp/development.md](../../docs/psp/development.md)
- Installing and playing: the [README](../../README.md)

How the PSP differs from the PC builds, in short:

| Difference | What the port does |
|---|---|
| A program cannot choose its addresses | The game heap is one block from the system, the hardware registers are arrays (`PORT_HW`), and the data tables are found through a generated table of addresses (`psp_mem.c`) |
| 68,611 loose files are impractical on a memory stick | The disc's three AFS archives are read as they are (`psp_file.c`) |
| No SDL GPU back end on the PSP | A renderer on the PSP's graphics engine (`psp_gu.c`) behind the PC's GS front end, and the dozen SDL3 audio calls the PC's sound code uses, on the PSP's audio output (`sdlshim/`, `psp_audio.c`) |
| One analog stick, no L2 / R2 / L3 / R3 | `psp_gfx.c` and `psp_controls.h`: portable controls are embedded by default during live fights; legacy mapping remains available in source only |
| 2 MB of graphics memory, shared by the frame buffer, the depth buffer and the textures | One pair of 480 x 272 16-bit frame buffers at the PSP's screen size, the game's frame-buffer pixels scaled into them, and a texture cache of what is left |
| No vertex shaders | Native validated VU1 paths for fighters and stage strips with guarded interpreter fallback (`port/src/gs/gs_vu1.c`, `psp_stage4.h`) |
| 333 MHz, one core, and no floating point to spare | The game's vector library as its portable reference (`src/port/vu0_a.c`, `vu0_b.c`: the PS2's own arithmetic model), and the culling of the Vita port (`eft_c.c`, `port_fast.c`) |
