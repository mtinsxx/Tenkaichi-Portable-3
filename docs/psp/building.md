# Building the PSP version

The build runs on Linux (or WSL on Windows) and needs no game files: the game's built-in data tables are in the
repository as their shape only (`port/data`) and are filled in from the player's disc when the program starts.

## Requirements

- The [pspdev](https://github.com/pspdev/pspdev) toolchain: `psp-gcc` for the PSP's MIPS Allegrex processor and
  [pspsdk](https://github.com/pspdev/pspsdk) (its `sceGu` graphics library, the stub libraries and the tools that
  make an `EBOOT.PBP`). `PSPDEV` must point to it and `$PSPDEV/bin` be on the `PATH`.
- Python 3, git, a host `gcc` and `make`.
- libclang and its Python bindings (**optional**: they are used to write the PS2 compiler's evaluation order of
  call arguments into the game's code before it is compiled). On Ubuntu / Debian: `sudo apt install libclang-dev
  python3-clang`, or `pip install libclang`. Without them the build prints a note and compiles the sources as they
  are; gcc for MIPS already evaluates arguments right to left, as the PS2's compiler did.

## Steps

```sh
export PSPDEV=/usr/local/pspdev
export PATH=$PSPDEV/bin:$PATH

python3 port/psp/build.py
```

The result is `port/build/psp_rel/EBOOT.PBP` (about 4 MB). A full build takes a few minutes on four cores;
`BT3_INCREMENTAL=1` reuses the objects of the previous one where the source is older (headers are not tracked).

Other environment variables:

| Variable | Meaning |
|---|---|
| `PSPDEV` | The pspsdk installation (default `/usr/local/pspdev`) |
| `BT3_PSP_DEV=1` | The development build, see [development.md](development.md) |
| `BT3_INCREMENTAL=1` | Reuse the objects of the previous build |
| `BT3_LIBCLANG` | libclang's shared library, when it is not found in the usual places |
| `BT3_CLANG_PY` | The folder that holds `clang/cindex.py`, when the bindings are not installed |

`python3 port/psp/build.py compile`, `link` or `pbp` runs one step only. The linked program is kept as
`port/build/psp_rel/bt3.elf` with its `bt3.map`, for reading crash dumps.

## What the build does

The game's code is compiled as in the PC build's 32-bit variant: the same sources, the PS2 compiler's float
constants (`port/tools/eeconst.py`), and call arguments evaluated right to left as the PS2 compiler does
(`port/tools/ptr32.py`, through libclang).

MIPS agrees with the PS2's own processor on everything else the ports have to bridge, which is why the PSP build
is the shortest of them:

| Difference | The PC build | The Vita | The PSP |
|---|---|---|---|
| 32-bit pointers | rewritten for 64 bits | as the PS2 | as the PS2 |
| Plain `char` | signed (`-fsigned-char`) | signed | signed |
| A shift by 32 or more | the x86 rule | every shift rewritten (`srcfix.py`) | the PS2's rule, the processor's own |
| Call arguments | evaluated right to left | rewritten to right to left | right to left (checked with `ptr32.py`) |
| Addresses | the PS2's own | `PORT_HW()` for the registers | `PORT_HW()` |

The port's own code and the renderer front end (`port/src/gs`) are compiled as ordinary PSP code. The few names
that exist both in the game's libraries and in the C library's are renamed in the objects
(`objcopy --redefine-syms`).

The build generates, under `port/build/gen/psp_rel/`, the table of where each of the game's data tables gets its
values on the disc (`psp_dat.c`) and the stubs for the functions of the PS2's libraries that nothing provides
(`stubs.c`; each stops the program with the function's name, and none of them is reached in the game).

## Running it

Copy `EBOOT.PBP` to `ms0:/PSP/GAME/BT3/` on the memory stick, and the files of your own USA disc
(SLUS-21678) to `ms0:/PSP/GAME/BT3/disc/`, as they lie on it — `SLUS_216.78`, `BIN/DBZP.BIN` and `DATA/`.
Every run writes `ms0:/PSP/GAME/BT3/log.txt`. `LOG_ALL` is built into the diagnostic EBOOT, which does not read `bt3.txt`.

## Running it in PPSSPP

[PPSSPP](https://github.com/hrydgard/ppsspp) runs the program on a PC, which is how this port is being tested
while it is being written. Build its headless front end (no window, no OpenGL):

```sh
git clone --recursive https://github.com/hrydgard/ppsspp
cmake -S ppsspp -B ppsspp/build -DHEADLESS=ON -DHEADLESS_CROSS=ON -DUNITTEST=OFF \
      -DNO_OPENGL=ON -DUSE_FFMPEG=OFF -DUSE_MINIUPNPC=OFF -DUSING_X11_VULKAN=OFF \
      -DUSE_DISCORD=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build ppsspp/build --target PPSSPPHeadless -j$(nproc)
```

Then point it at a memory stick of its own and at the program:

```sh
mkdir -p /tmp/psp/PSP/GAME/BT3
cp port/build/psp_rel/EBOOT.PBP /tmp/psp/PSP/GAME/BT3/
ppsspp/build/PPSSPPHeadless --memstick=/tmp/psp /tmp/psp/PSP/GAME/BT3/EBOOT.PBP \
    --timeout=20 --log --screenshot-at=10:/tmp/psp/shot.png
```

`--log` prints every call the program makes to the PSP's operating system, which is how a crash is found;
`--screenshot-at` writes the screen at a time of the run. The program's own log is
`/tmp/psp/PSP/GAME/BT3/log.txt`.

PPSSPP keeps a homebrew program's user memory at 32 MB unless its `PARAM.SFO` asks for more, so without the
`MEMSIZE=1` that `build.py` writes the 42 MB heap of the C library cannot be had and the program stops with
"no memory for the game heap" — a message that is about the emulator's setting, not about the port.

## Size and memory

The program is loaded as about 12 MB (4 MB of it code and data, the rest zeroed: the Graphics Synthesizer's
4 MB of memory, the sound processor's 2 MB, the renderer's own buffers). The game's heap is the PS2's whole
region, 29 MB, in one block taken from the C library's heap, which is 42 MB (`port/psp/psp_mem.c`,
`port/psp/psp_main.c`); the rest of it is the port's: the vertex batch, the textures being decoded, the file
buffers. The disc's files are read in pieces, never held whole, so their size does not count.

All of it needs a console with 64 MB of memory: a PSP-2000, 3000, Go or E1000, and `MEMSIZE=1` in `PARAM.SFO`
(the build writes it) so that the system hands the program the whole user memory and not 32 MB of it. On a
PSP-1000 the heap cannot be had and the program says so at start. Heap size is fixed to preserve the PS2 memory layout and cannot be changed by a TXT file.

The PSP's 2 MB of graphics memory holds two 480 x 272 frame buffers, the depth buffer and the textures
(`port/psp/psp_gu.c`); a texture too big for what is left is left out of the picture rather than stopping
the program.
