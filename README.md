# Tenkaichi Portable 3 — native PSP port

A native PSP port of **Dragon Ball Z: Budokai Tenkaichi 3** (PlayStation 2, USA release, SLUS-21678), based on
[arielxflores' PS Vita port](https://github.com/arielxflores/Tenkaichi3Decomp-Vita), itself built on
z3xox's [Tenkaichi3Decomp](https://github.com/z3xox/Tenkaichi3Decomp), the PC port of the BT3-Decompiled
decompilation. The game's own code is compiled for the PSP, with a renderer for the PSP's graphics engine (sceGu),
sound, and controls written for the handheld. It is not an emulator.

This repository contains **no game data**. The game's files are copied from your own disc of the USA release to
the memory stick, see [Game files](#game-files).

This is an unofficial port. It is not affiliated with the Tenkaichi3Decomp project; please report problems with
the PSP version here, not upstream.

## PSP-only focus and upstream attribution

**Tenkaichi Portable 3 is a native PlayStation Portable (PSP) port, not a PS Vita release.**
It builds its own PSP executable and PSP renderer. The PS Vita port by
[arielxflores](https://github.com/arielxflores/Tenkaichi3Decomp-Vita), the
[Tenkaichi3Decomp project by z3xox and contributors](https://github.com/z3xox/Tenkaichi3Decomp),
and the earlier BT3 decompilation are credited as foundations of this work; their authorship is not
replaced by PSP-specific adaptations. See [Credits](#credits), [THIRD_PARTY.md](THIRD_PARTY.md),
and the corresponding license notices.

Decorative gameplay screenshots and inherited Vita LiveArea graphics have been removed from the
current PSP-focused source tree because they are not needed to build the PSP EBOOT. Legacy Vita
source code may remain for provenance and comparison; its presence does not make this a Vita port.


## Publication and independent testing

This is an experimental, unofficial PSP port maintained in this repository by [mtinsxx](https://github.com/mtinsxx). It is based on the earlier work credited below; maintainership is not a claim of authorship of the original game or its decompilation.

Public source code, if published, may be downloaded, compiled and tested by third parties according to the applicable rights and licenses. Development `EBOOT.PBP` artifacts and releases should not be assumed to be stable or fully tested. Before changing the repository's visibility, follow the [PSP publication and privacy checklist (pt-BR)](docs/psp/PUBLICATION_CHECKLIST_PTBR.md).

## Status

What is built and what has been tested is written at the end of this file, in [Status](#status-1). Read it before
you put the program on a console: this is a port in progress, and it says there exactly which parts have been seen
running and which have not.

## What works

- The project builds to an `EBOOT.PBP` with the pspdev toolchain, and the game's own code - the decompiled
  PlayStation 2 sources - compiles for the PSP's MIPS Allegrex processor.
- The game's data tables are read from your own disc and checked against the unmodified USA release, as in the
  other ports.
- The renderer draws what the game draws: textured triangles, sprites and lines with the PS2's blending, alpha and
  depth tests, and the textures of GS memory, at the PSP's 480 x 272.
- The controller, with SELECT as a shift key for the four buttons the PSP has not (see [Controls](#controls)).
- Sound: the PC port's sound code on the PSP's audio output.

Not finished: the game's screen effects (glare, glow, blur and the depth tint are built by drawing one frame
buffer into another, and the PSP's 2 MB of graphics memory has room for one pair of frame buffers), the movies,
and the speed - the VU1 microprograms are interpreted instruction by instruction, which is slow.

Not available: playing against another person (the PS2's two-controller mode), as on the Vita.

## Installation

Requirements:

- A PSP with 64 MB of memory (2000, 3000, Go or E1000): the game's heap is 29 MB, see
  [docs/psp/building.md](docs/psp/building.md#size-and-memory). A PSP-1000 says so at start.
- Custom firmware that runs unsigned homebrew (any recent one), or [PPSSPP](https://www.ppsspp.org/) to try it on
  a computer.
- About 3 GB free on the memory stick for the game files.

Steps:

1. Copy `EBOOT.PBP` to `ms0:/PSP/GAME/BT3/` on the memory stick (make the folder; `BT3` is its name).
2. Copy the game files to `ms0:/PSP/GAME/BT3/disc/` as described below.
3. Start **Dragon Ball Z: BT3** from the XMB (Game -> Memory Stick).

## Game files

You need a disc image (`.iso`) of your own copy of the USA release, SLUS-21678. Other releases (PAL, Japanese) are
not supported. From the image, copy `SLUS_216.78` and the `BIN` and `DATA` folders, so that the memory stick has:

```
ms0:/PSP/GAME/BT3/disc/SLUS_216.78
ms0:/PSP/GAME/BT3/disc/BIN/DBZP.BIN
ms0:/PSP/GAME/BT3/disc/DATA/PZS3US0.AFS
ms0:/PSP/GAME/BT3/disc/DATA/PZS3US1.AFS
ms0:/PSP/GAME/BT3/disc/DATA/PZS3US2.AFS
ms0:/PSP/GAME/BT3/disc/DATA/ZS3USOP.PSS        (and the other files of the DATA folder)
```

- **Windows:** open the `.iso` with [7-Zip](https://www.7-zip.org/), select `SLUS_216.78`, `BIN` and `DATA` and
  extract them into a folder named `disc`.
- **Linux / macOS:** `7z x -odisc your-image.iso SLUS_216.78 BIN DATA`

Then copy the `disc` folder to `ms0:/PSP/GAME/BT3/` (USB or a card reader). The game checks at start that the
files are the unmodified USA release and tells you if they are not.

## Controls

The default `legacy` mapping is unchanged. For an **experimental PSP/PPSSPP-friendly layout**, add
`controls=portable` to `ms0:/PSP/GAME/BT3/bt3.txt` and restart. Only while
**actively fighting**, the analog stick moves, D-pad UP/DOWN flies up/down, L charges Ki and R
locks on. L + UP/DOWN retains the original PS2 direction for supers and Ultimate;
L + R + UP/DOWN allows charged vertical flight.

**SELECT controls the missing R3:** tap or hold for transformation; SELECT + direction
sends the game's actual R3 + direction for transformations/fusions (according to the
fighter's Skill List); SELECT + L + direction provides an alternate, comfortable fusion
gesture **without accidentally sending L2**. SELECT + Square sends L3+R3 for team switch.
A short SELECT-chord grace window and latched modifiers reduce unintended transformations.

The original pad mapping is restored automatically in paused menus, cutscenes and other
non-fight states using the game's battle flags; this is not yet tested end-to-end in PPSSPP.
See [full PSP control reference, timings and limitations](docs/psp/CONTROLS_ROADMAP_PTBR.md)
(in Portuguese). To roll back, remove the setting or set `controls=legacy`.

The **default legacy** control type:

| PSP | Action |
|---|---|
| Analog stick or D-pad | Move |
| Square | Fight |
| Triangle | Ki Blast |
| Cross | Dash |
| Circle | Guard |
| L | Ki Charge |
| R | Ascend |
| SELECT + L | Blaster Wave (L2) |
| SELECT + R | Descend (R2) |
| SELECT + Square | L3 |
| SELECT + Circle | R3 |
| START | Pause |

- The analog stick is the PS2's left stick; the PSP has no second one, so the right stick is never moved.
- **Options -> Controller -> Control Type -> Custom** assigns the actions to other buttons, as on the PS2. The
  pause menu's skill list shows each command with the buttons of the control type in use.
- Holding START + SELECT for a second and a half closes the program (as on a console the PSP has no way out of a
  game besides its own menus: the HOME button is the system's).

## Notes

- Saves (a virtual memory card) and settings are kept in `ms0:/PSP/GAME/BT3/`: `saves/` and `bt3_settings.txt`.
  `log.txt` there describes the last run, which helps with bug reports.
- Optional settings go in `ms0:/PSP/GAME/BT3/bt3.txt`, one per line: `wide=0` shows the PS2's original 4:3
  picture, `disc=<folder>` reads the game files from another folder, `report=1` writes the speed into the log
  every two seconds.
- The port runs the PSP at its standard clocks (333 MHz): no overclocking.

## Building

See [docs/psp/building.md](docs/psp/building.md). The build needs the pspdev toolchain (psp-gcc and pspsdk),
Python 3 and a host gcc; no game files are needed to build. A development build with test options is described in
[docs/psp/development.md](docs/psp/development.md).

The rest of the repository is Tenkaichi3Decomp as of the Vita port's commit
[`dd6b20c`](https://github.com/z3xox/Tenkaichi3Decomp/commit/dd6b20ce13dc01871983a8176196f5cb772fa7a8), with the
changes the handhelds needed in the shared code, most of them inside `#ifdef PORT_CONSOLE` (both ports) or
`#ifdef PORT_PSP`. Its own README is [docs/port/README-PC.md](docs/port/README-PC.md). The PC build is not
maintained here: for the PC version, use [upstream](https://github.com/z3xox/Tenkaichi3Decomp).

## Status

*Nothing is claimed here that has not been seen running.*

### Seen running

In PPSSPP (headless, built from source; see
[docs/psp/building.md](docs/psp/building.md#running-it-in-ppsspp)), with no disc files present:

- The `EBOOT.PBP` loads, the program's module starts and runs as a PSP program.
- The 42 MB heap of the C library is allocated, and the game's 29 MB heap is taken from it.
- `sceGu` starts: the display is set to 480 x 272 and the frame and depth buffers are in place.
- The text screen is drawn: with no disc files, the program puts on the screen, and into
  `ms0:/PSP/GAME/BT3/log.txt`, which file it is missing, and waits for START.

### Not yet seen running

Everything past the disc check. The port has no game data of its own - the repository must not carry any, and
the data tables are read from the player's disc - so no run has yet got further than "the game's program was
not found". Until one does, the renderer, the sound, the menus, the fights and the speed are **untested**,
and the list under [What works](#what-works) above is what the port is written to do, not what has been
watched doing it.

## Credits

- [mtinsxx](https://github.com/mtinsxx) for maintaining this PSP port repository and coordinating PSP-specific graphics, performance, diagnostics and integration work documented in its commit history. Attribution for underlying work remains with its original authors.
- [arielxflores](https://github.com/arielxflores) for
  [Tenkaichi3Decomp-Vita](https://github.com/arielxflores/Tenkaichi3Decomp-Vita), the port this one is built from,
  and [z3xox](https://github.com/z3xox) and the contributors of
  [Tenkaichi3Decomp](https://github.com/z3xox/Tenkaichi3Decomp), the PC port that one is built on, and the authors
  of the BT3-Decompiled decompilation it uses
- Spike, the developer of Dragon Ball Z: Budokai Tenkaichi 3
- The [pspdev](https://github.com/pspdev) team: the toolchain and pspsdk this port is compiled with
- The third-party code and data used by the port are listed in [THIRD_PARTY.md](THIRD_PARTY.md).

## License

The PSP port's code (`port/psp/` and the changes for the PSP in the rest of the tree) is under the
[GNU Affero General Public License, version 3](LICENSE), the license of Tenkaichi3Decomp's port code since
October 2026. The port code it is based on, from before that change, came under the MIT license; its notice is
kept in [LICENSE-MIT](LICENSE-MIT).

The game's code in `src/` and `include/` comes from the decompilation and belongs to the game's rights holders.
This project is not affiliated with or endorsed by them, and distributes none of the game's data. You need your
own legally obtained copy of the game to use it.
