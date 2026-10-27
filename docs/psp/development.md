# The development build

```sh
BT3_PSP_DEV=1 python3 port/psp/build.py
```

builds `port/build/psp_dev/EBOOT.PBP`: the same program as the release, with its own title ("BT3 (development)")
so that it can be installed next to it, and every setting of `bt3.txt` read. It shares `ms0:/PSP/GAME/BT3/`
(the disc files, saves and settings) with the release.

## `ms0:/PSP/GAME/BT3/bt3.txt`

One `name=value` per line; `#` starts a comment. The settings read by both builds:

| Setting | Meaning |
|---|---|
| `disc=<folder>` | Another place for the game files |
| `wide=0` | The PS2's 4:3 picture in the middle of the screen (the default: the picture filled to 16:9) |
| `heap=<MB>` | Ignored with a warning: game heap is fixed at 29 MiB for PS2 memory layout compatibility |
| `report=1` / `report=0` | Enable / disable two-second speed, draw and texture diagnostics in the log (default: 1) |
| `gs_depth=0|1|2` | GE depth test/write from the GS `TEST`/`ZBUF` registers: `0` off (default, submission order decides), `1` every target, `2` screen target only (work passes keep the scene's one depth buffer intact). Diagnostic for fighters/stages drawing inside-out; see [DIAGNOSTICO_FPS_TEXTURAS_2026-10-26.md](DIAGNOSTICO_FPS_TEXTURAS_2026-10-26.md) |
| `mode=demo` | A computer against computer fight instead of the menus |
| `demopick=<n>`, `demostage=<id>`, `demochara1=<id>`, `demochara2=<id>` | Which test fight |

## The log

`ms0:/PSP/GAME/BT3/log.txt`, written by every run (the release overwrites it, the development build too). It has
the start-up, the messages of the file and data loading, and with `report=1` every two seconds:

```
psp: 61 blanks in 2.00 s = 30.4 a second = 101% of the console's speed (draws 41231, batches 1902, new textures 33, left out 210)
```

`blanks` is the game's own count of vertical blanks, so 59.94 a second is the console's speed (30.4 a second is a
fight: the game waits for two blanks a frame). `draws` is the number of primitives `gs_core.c` handed to the
renderer in that time, `batches` how many times the PSP's graphics engine was given work, `new textures` how many
textures had to be decoded out of GS memory, and `left out` how many primitives the renderer does not draw
(the passes that only make sense on the PS2's memory layout).

## Where things are

| Path | Contents |
|---|---|
| `port/psp/build.py` | The build |
| `port/psp/psp_main.c` | Start-up, settings, the log, the ways out |
| `port/psp/psp_gu.c` | The renderer (sceGu): textures, blending, the batches, the frame |
| `port/psp/psp_gfx.c` | The controller, the vertical blanks' pace, the report |
| `port/psp/psp_audio.c`, `sdlshim/` | Sound output, under the PC port's sound code (`port/src/gs/snd_adx.c`, `snd_se.c`) |
| `port/psp/psp_file.c`, `psp_mem.c` | The disc's archives, memory and the game's data tables |
| `port/src/gs/gs_core.c` | The PC port's GS front end, with the PSP's changes |
| `port/build/psp_dev/bt3.elf`, `bt3.map` | The linked program, for reading a crash dump |

## What is not finished

- **The effects**: the game's glare, glow, blur and the depth tint are built by drawing a frame buffer into another
  and sampling it back. The PSP has one pair of frame buffers at the size of the screen, so those passes copy the
  whole picture instead of a shrunken one: the picture gets brighter where the console would glow.
- **The fighters' outlines and their ground shadows** need the work buffers the same way (`gfxm_e_b.c`).
- **The VU1 programs are interpreted** (`gs_vu1.c`, `BT3_VU_INTERP`): the PSP has no vertex shaders, so the
  original microprograms are run instruction by instruction. This is the reference the picture has to be checked
  against, and it is the first thing to replace for speed.
- **The movies** (`plat_movie.c`, the MPEG decoder of the opening and ending films) build but are not wired up.

## Testing without a disc

The repository carries no game data, so most of what has to be checked needs a disc of one's own. Two things can
be checked without one, both in PPSSPP ([building.md](building.md#running-it-in-ppsspp)):

**That the disc's files are read right.** Put stand-ins where the program looks for them: a well-formed 32-bit
little-endian ELF at `disc/SLUS_216.78` with one `PROGBITS` section whose address lies in `0x100000..0x2FF180`,
and 512 KB of anything at `disc/BIN/DBZP.BIN` (`gPortDatRuns` needs the first 0x7C280 bytes of it). The program
then reads the section, seeks to each of its 2 388 runs and copies it, works out the checksum of them all and
stops with

```
FATAL: SLUS_216.78 / BIN/DBZP.BIN in
  ms0:/PSP/GAME/BT3/disc
are not the unmodified USA release (SLUS-21678).
```

which is the whole of `Psp_LoadGameData` - the parsing, the seeking, the reading and the check - saying the only
thing wrong with the files is that they are not the game's.

**That the program starts.** With the check above taken out for the run only (never in a commit), the game's
thread starts on zeroed tables, and what it reaches before the nonsense stops it is worth knowing: it opens the
sound output, and the first file it asks for is `disc/DATA/<archive>`, which is the path the disc's files have to
be copied to. Do not read a crash past that point as anything but the tables being empty.
