#!/usr/bin/env python3
"""PSP build of Tenkaichi3Decomp.

    python3 port/psp/build.py [compile] [link] [pbp]       (no step named: all three)

Environment:
    PSPDEV             the pspsdk installation (default /usr/local/pspdev)
    BT3_PSP_DEV=1      the development build: its own title ("BT3 (development)"), the log's figures, a test
                       fight with the demo's pairing (bt3.txt demoduel=1, demostage=, demochara1=, demochara2=)
    BT3_INCREMENTAL=1  reuse objects newer than their source (headers are not tracked)

The game's code is compiled as in the PC build's 32-bit variant: the same sources, the PS2 compiler's float
constants (eeconst.py) and call arguments evaluated right to left (ptr32.r2l). MIPS agrees with the PS2 on
everything else the ports have to bridge: 4-byte pointers, a signed plain char, shift counts taken modulo 32
(so srcfix.py, the Vita's rule for shifts, is not needed here) and right-to-left arguments.
The game's built-in data tables come from port/data as shape only; their values are read from the player's own
disc when the program starts. Objects go to port/build/obj_psp_<variant>, the program to port/build/psp_<variant>/.
"""
import concurrent.futures, os, pathlib, re, shutil, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/tools"))
import portsrc, eeconst

SDK = pathlib.Path(os.environ.get("PSPDEV", "/usr/local/pspdev"))
BIN = SDK / "bin"
P = str(BIN / "psp-")
DEV = os.environ.get("BT3_PSP_DEV") == "1"
TAG = "_dev" if DEV else "_rel"
OBJ = ROOT / ("port/build/obj_psp" + TAG)
OUT = ROOT / ("port/build/psp" + TAG)
GEN = ROOT / ("port/build/gen/psp" + TAG)
TITLE = "BT3 (development)" if DEV else "Dragon Ball Z: BT3"
ELF = OUT / "bt3.elf"

# Names that exist both in the game's side and in the C library's with other meanings: the internals of the PS2's
# maths library (port/third_party/newlib_libm), the PS2 C library's character table, and the generic soft-float
# entry points of softfloat_ps2.c. Their definitions are renamed in those objects (objcopy --redefine-syms).
X_DEFS = ["addsf3", "subsf3", "mulsf3", "divsf3", "negsf2", "eqsf2", "nesf2", "ltsf2", "lesf2", "gtsf2", "gesf2", "unordsf2",
          "fixsfsi", "fixunssfsi", "floatsisf", "floatunsisf"]
RENAME = {f"__{n}": f"PortX_{n}" for n in X_DEFS}
RENAME |= {n: "PortNL_" + n.lstrip("_") for n in ["__ieee754_acosf", "__ieee754_asinf", "__ieee754_atan2f", "__ieee754_powf",
           "__ieee754_rem_pio2f", "__ieee754_sqrtf", "__kernel_cosf", "__kernel_rem_pio2f", "__kernel_sinf", "__kernel_tanf"]}
RENAME |= {"_ctype_": "PortPS2_ctype_"}
# softfloat_ps2.c has its own copies of the vector library's three primitives: renamed, so that the library's own
# (the exact model of the PS2's float unit) are the ones everything calls.
RENAME_SOFTFLOAT = {"RefVu0_AddBits": "PortX_VuAdd", "RefVu0_MulBits": "PortX_VuMul", "RefVu0_DivBits": "PortX_VuDiv"}

# The PS2 compiler and gcc for MIPS agree on these; they are given anyway, so that the game's code is compiled the
# same way on every port.
ABI = ["-fsigned-char", "-fno-short-enums", "-fsigned-bitfields"]
# the game is C as the PS2's compiler took it, and gcc 14 makes some of that an error by default
PERMISSIVE = ["-Wno-error=implicit-function-declaration", "-Wno-error=implicit-int", "-Wno-error=int-conversion",
              "-Wno-error=return-mismatch", "-Wno-error=incompatible-pointer-types",
              "-Wno-error=builtin-declaration-mismatch"]
# -G0: nothing in the small-data sections. The MIPS processor reaches those through one register (gp) with a
# 16-bit offset, which gives 64 KB for the whole program - the game's and the port's globals are far more.
COMMON = ["-O2", "-g", "-G0", "-fno-strict-aliasing", "-ffp-contract=off", "-w", "-DPORT_PSP=1", "-DPORT_CONSOLE=1",
          "-DMPEG2_PLAYER=1", "-DPSP=1"] + PERMISSIVE + (["-DBT3_PSP_DEV=1"] if DEV else [])
# As in the PC build, no call of the C library is replaced by the compiler's own idea of it; the memory functions
# are allowed (small memcpy / memset calls become inline code).
NOBUILTIN = ["-fno-builtin-" + n for n in "sinf cosf tanf asinf acosf atanf atan2f sqrtf powf floorf ceilf fabsf fmodf expf logf sin cos tan sqrt "
             "pow floor ceil fabs abs labs rand srand printf sprintf snprintf fprintf vsprintf puts putchar fputs fwrite strcpy strncpy strcat "
             "strncat strlen strcmp strncmp strchr strrchr strstr malloc calloc realloc free exit abort atoi memcmp memmove".split()]
SDK_INC = f"-I{SDK / 'psp' / 'sdk' / 'include'}"
GAME = [P + "gcc", "-std=gnu89", "-c"] + COMMON + ABI + NOBUILTIN + \
       ["-include", "port_libm.h", "-Iinclude", "-Iport/include", "-Iport/psp", SDK_INC, "-include", "port_compat.h"]

# The game's compiler evaluates call arguments from right to left and the game's code depends on it in places; the
# preprocessed text is rewritten as in the 64-bit PC build (ptr32.r2l, libclang) to be sure of it here too.
R2L_ARGS = ["-x", "cpp-output", "--target=mipsel-none-eabi", "-mfloat-abi=hard", "-std=gnu89", "-w",
            "-Wno-error=return-type", "-Wno-error=return-mismatch"]


def r2l(text):
    """The game's preprocessed text with call arguments evaluated right to left (ptr32.r2l)."""
    if os.environ.get("BT3_CLANG_PY"):
        sys.path.insert(0, os.environ["BT3_CLANG_PY"])
    import clang.cindex as ci
    if not ci.Config.loaded:
        lib = os.environ.get("BT3_LIBCLANG")
        if lib:
            ci.Config.set_library_file(lib)
    import ptr32
    return ptr32.r2l(text.encode("latin-1"), R2L_ARGS).decode("latin-1")


def compile_ee(cmd, src, o, game=False):
    """Preprocess, rewrite the float constants as the PS2 compiler read them (eeconst.py) and, for the game's
    sources, the calls whose arguments have side effects (r2l), compile."""
    r = subprocess.run([a for a in cmd if a != "-c"] + ["-E", str(src)], cwd=ROOT, capture_output=True, text=True, encoding="latin-1")
    if r.returncode:
        return r
    i = pathlib.Path(str(o)[:-2] + ".i")
    text = eeconst.transform(r.stdout)
    if game:
        try:
            text = r2l(text)
        except Exception as e:  # no libclang, or text it cannot parse
            print(f"  (r2l unavailable: {e}; compiling as it is)")
    i.write_text(text, encoding="latin-1")
    return rename(subprocess.run(cmd + ["-fpreprocessed", str(i), "-o", str(o)], cwd=ROOT, capture_output=True, text=True), o)


def rename(r, o):
    """After a compile: the clashing definitions renamed (see RENAME)."""
    if r.returncode == 0:
        m = OBJ / "rename" / (o.name + ".txt")
        m.parent.mkdir(exist_ok=True)
        syms = RENAME | (RENAME_SOFTFLOAT if o.name == "pc_softfloat_ps2.o" else {})
        m.write_text("".join(f"{a} {b}\n" for a, b in syms.items()))
        r2 = subprocess.run([P + "objcopy", f"--redefine-syms={m}", str(o)], capture_output=True, text=True)
        if r2.returncode:
            return r2
    return r


def cc_game(f):
    o = OBJ / (str(f.relative_to(ROOT / "src")).replace("/", "_")[:-2] + ".o")
    src, done, missing = portsrc.prepare(f)
    if os.environ.get("BT3_INCREMENTAL") and o.exists() and o.stat().st_mtime > max(src.stat().st_mtime, f.stat().st_mtime):
        return o, f, ""
    r = compile_ee(GAME + [f"-I{f.parent}"], src, o, game=True)
    return (o if r.returncode == 0 else None), f, r.stderr


def first_error(e):
    return next((l for l in e.splitlines() if "rror" in l), e.strip()[:200])[:260]


def data_tables():
    """port/data/*.s assembled for MIPS, each with a label at its first byte, and the list of where every run of
    values comes from on the player's disc as a C table (psp_dat.c)."""
    GEN.mkdir(parents=True, exist_ok=True)
    where = {}
    checksum = None
    for l in (ROOT / "port/data/index.txt").read_text().splitlines():
        p = l.split()
        if len(p) == 3 and not l.startswith("#"):
            where[p[0]] = (int(p[1]), int(p[2], 16))
        elif len(p) == 2 and p[0] == "checksum":
            checksum = int(p[1], 16)
    objs, decl, rows, total = [], [], [], 0
    names = sorted(where) + sorted(f.name[:-2] for f in (ROOT / "port/data").glob("*.s") if f.name[:-2] not in where)
    for name in names:
        which, so = where.get(name, (None, 0))
        label = "PortTbl_" + re.sub(r"\W", "_", name)
        text = (ROOT / "port/data" / (name + ".s")).read_text()
        off, lines = 0, []
        for l in text.splitlines():
            lines.append(l)
            p = l.split()
            if p and p[0] == ".p2align" and label not in "\n".join(lines):
                lines += [f".globl {label}", f"{label}:"]
            if not p or p[0].startswith("#"):
                continue
            if p[0] == ".space":
                if which is None:
                    raise SystemExit(f"{name}: values expected from the disc, but the table is not in port/data/index.txt")
                rows.append(f"    {{{label} + {off}, {which}, 0x{so + off:X}, {int(p[1])}}},")
                total += int(p[1])
                off += int(p[1])
            elif p[0] == ".zero":
                off += int(p[1])
            elif p[0] == ".long":
                off += 4
        decl.append(f"extern unsigned char {label}[];")
        s = GEN / (name + ".s")
        s.write_text("\n".join(lines) + "\n")
        o = OBJ / ("data_" + re.sub(r"\W", "_", name) + ".o")
        r = rename(subprocess.run([P + "as", str(s), "-o", str(o)], capture_output=True, text=True), o)
        if r.returncode:
            print("FAILED", name, r.stderr.splitlines()[0][:200])
        else:
            objs.append(str(o))
    (GEN / "psp_dat.c").write_text(
        "/* generated by port/psp/build.py from port/data: where the game's data tables get their values */\n"
        '#include "psp_dat.h"\n' + "\n".join(decl) + "\nconst PortDatRun gPortDatRuns[] = {\n" + "\n".join(rows) +
        f"\n}};\nconst unsigned gPortDatCount = {len(rows)};\nconst unsigned long long gPortDatChecksum = 0x{checksum:X}ull;\n")
    print(f"data tables: {len(objs)} files, {total} bytes in {len(rows)} runs to read from the disc")
    return objs


def port_sources():
    """(file, kind): 'ee' game-side code (PS2 constants), 'gs' the renderer front end and sound (as on PC),
    'hard' the PSP's own code, 'vu0' the vector-library references."""
    S, V = ROOT / "port/src", ROOT / "port/psp"
    out = [(ROOT / "src/port/vu0_a.c", "vu0"), (ROOT / "src/port/vu0_b.c", "vu0")]
    # of port/src: everything but the PC's memory, files, crash report and maths bridge (psp_*.c take their place)
    # port_vufast.c is left out: its fast stand-ins have ARM / x86 inline assembly in them (the PSP takes the
    # vector library's portable reference, src/port/vu0_a.c and vu0_b.c, as the Vita does for the exact model)
    for n in ("gs_marker", "headless", "mathf_pc", "plat_gs", "plat_mc", "plat_movie", "plat_mpeg2",
              "plat_settings", "plat_stub", "plat_sys", "softfloat_ps2", "port_fast"):
        out.append((S / (n + ".c"), "gs" if n in ("plat_movie", "plat_mpeg2") else "ee"))
    out += [(V / "psp_mem.c", "hard"), (V / "psp_file.c", "hard"), (V / "psp_main.c", "hard"),
            (V / "psp_battle_state.c", "hard"),
            (GEN / "psp_dat.c", "hard"), (S / "gs/gs_sony.c", "hard"),
            (V / "psp_gfx.c", "hard"), (V / "psp_native_mesh_preview.c", "hard"),
             (V / "psp_native_fighter_packet.c", "hard"), (S / "gs/gs_core.c", "gs"), (S / "gs/gs_vu1.c", "gs"),
            (V / "psp_gu.c", "hard"), (V / "psp_audio.c", "gs"), (S / "gs/snd_adx.c", "gs"), (S / "gs/snd_se.c", "gs"),
            (V / "psp_wrap.c", "gs")]
    return out


def cc_port(item):
    f, kind = item
    o = OBJ / ("pc_" + f.stem + ".o")
    base = [P + "gcc", "-std=gnu99", "-c"] + COMMON + \
           ["-Iinclude", "-Iport/include", "-Iport/src", "-Iport/psp", "-Iport/src/gs", SDK_INC]
    if kind == "gs":  # front end, VU1, audio: ordinary float code (separate from the game's strict PS2 float ABI)
        o = OBJ / ("gs_" + f.stem + ".o")
        # VU1/GS interpreters and the PSP's standalone MPEG2 intra
        # decoder are CPU hotspots: enable -O3 ONLY for those three
        # portable C sources. No -ffast-math, no changed PS2 EE build
        # flags, no new GPU modes, no changes to the game's FPU ABI.
        # Guard movie pixels via the host differential RGB5551 test.
        gs_opt = "-O3" if f.name in ("gs_core.c", "gs_vu1.c", "plat_mpeg2.c") else "-O2"
        r = subprocess.run(base + [gs_opt, f"-I{ROOT / 'port/src/gs'}", f"-I{ROOT / 'port/psp/sdlshim'}",
                                   "-Dgetenv=Psp_GsGetenv", str(f), "-o", str(o)], cwd=ROOT, capture_output=True, text=True)
    elif kind == "hard":
        r = subprocess.run(base + ["-O2", f"-I{GEN}", str(f), "-o", str(o)], cwd=ROOT, capture_output=True, text=True)
    elif kind == "vu0":  # the vector-library references under the game's names, with the PS2's own arithmetic model
        r = rename(subprocess.run(base + ABI + ["-O2", "-include", "math.h", "-include", "stdlib.h", "-include", "port_libm.h",
                                                "-include", "vu0_names.h", str(f), "-o", str(o)],
                                  cwd=ROOT, capture_output=True, text=True), o)
    else:
        r = compile_ee(base + ABI + NOBUILTIN + ["-include", "math.h", "-include", "stdlib.h", "-include", "port_libm.h",
                                                 "-include", "port_compat.h"], f, o)
    return (o if r.returncode == 0 else None), f, r.stderr


def cc_libm(f):
    o = OBJ / ("nl_" + f.stem + ".o")
    r = compile_ee([P + "gcc", "-std=gnu89", "-c"] + COMMON + ABI + NOBUILTIN +
                   ["-Iport/include", f"-I{f.parent}", "-include", "newlib_shim.h"], f, o)
    return (o if r.returncode == 0 else None), f, r.stderr


def compile_all():
    OBJ.mkdir(parents=True, exist_ok=True)
    GEN.mkdir(parents=True, exist_ok=True)
    data = data_tables()
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as ex:
        res = list(ex.map(cc_game, portsrc.sources()))
        res += list(ex.map(cc_libm, sorted((ROOT / "port/third_party/newlib_libm").glob("*.c"))))
        res += list(ex.map(cc_port, port_sources()))
    bad = [(f, e) for o, f, e in res if o is None]
    for f, e in bad:
        print("FAILED", f.name, first_error(e))
    objs = [str(o) for o, _, _ in res if o] + data
    (OBJ / "objects.txt").write_text("\n".join(objs) + "\n")
    print(f"{len(objs)} objects, {len(bad)} failed")
    return not bad


# The libraries, in the order the PSP's tools want them: each library's own imports are in the ones behind it
# (libpspdebug draws through sceDisplay and sceGe, libpspgu through sceGe), and pspsdk's own three are added at
# the end by the compiler's spec.
LIBS = ["-lpspdebug", "-lpspgu", "-lpspgum", "-lpspaudio", "-lpspctrl", "-lpsppower", "-lpsputility",
        "-lpsprtc", "-lpspdisplay", "-lpspge", "-lpthreadglue", "-lpthread", "-lm"]

# Game functions replaced by faster equivalents (port/src/port_fast.c).
FAST_WRAPS = "StgFrustum_TestBox StgFrustum_TestPart BtlTaskList_Update BtlTaskList_Draw EftGfx_DrawPolyAvgZFront".split()
# Vector-library functions with an exact faster equivalent (port/src/port_vufast.c).
VUFAST_WRAPS = ""
# The port's own entry points around the game's (headless.c, psp_main.c) and the renderer's hooks.
PORT_WRAPS = "Progress_Main main exit Port_GsVif1Chain sceMpegGetPicture GsVu1_Call fread fopen".split()


def link():
    # Only the PS2's original middleware pointer tables still need traps.
    # Stop if any formerly reviewed reference moves into executable code
    # or changes its source table: it would need a real port implementation.
    subprocess.run([sys.executable, str(ROOT / "port/psp/check_missing_tables.py")],
                   cwd=ROOT, check=True)
    OUT.mkdir(parents=True, exist_ok=True)
    objs = (OBJ / "objects.txt").read_text().split()
    wraps = [f"-Wl,--wrap={n}" for n in PORT_WRAPS + FAST_WRAPS]
    cmd = [P + "gcc", "-Wl,-q", "-w", "-o", str(ELF), f"-L{SDK / 'psp' / 'sdk' / 'lib'}"] + \
          objs + LIBS + wraps + [f"-Wl,-Map={OUT / 'bt3.map'}"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    # functions the game's code refers to that do not exist on the PSP (the PS2's hardware libraries): each gets a
    # stub that stops the program with its name, should it ever be called
    need = sorted(set(re.findall(r"undefined reference to `(\w+)'", r.stderr)))
    # Every unresolved function used to be masked by a generated stub
    # without leaving an inspectable list. Save the complete baseline so
    # new missing functions can be reviewed instead of silently accepted.
    missing = GEN / "missing_symbols.txt"
    missing.write_text("".join(name + "\n" for name in need))
    # Map each unresolved symbol back to the object(s) referring to it.
    # The link error by itself hides this provenance and makes the list of
    # anonymous PS2 address names impossible to investigate safely.
    nm = subprocess.run([P + "nm", "-A", "-u"] + objs, capture_output=True, text=True)
    if nm.returncode:
        raise RuntimeError("psp-nm failed while examining unresolved symbols: " + nm.stderr[:400])
    callers = {name: set() for name in need}
    for line in nm.stdout.splitlines():
        m = re.match(r"^(.+?):\s+U\s+(\S+)\s*$", line)
        if m and m.group(2) in callers:
            callers[m.group(2)].add(pathlib.Path(m.group(1)).name)
    with (GEN / "missing_callers.tsv").open("w") as out:
        out.write("symbol\treferring_objects\n")
        for name in need:
            sites = ", ".join(sorted(callers[name])) or "(not mapped by nm)"
            out.write(f"{name}\t{sites}\n")
            print("PSP unresolved function:", name, "<-", sites)
    # The old 104 symbols mixed executable C calls with pointers stored
    # in original middleware tables. The two direct callers are now
    # implemented. Never regress to a direct missing game-code call.
    direct = {name: sorted(x for x in callers[name] if not x.startswith("data_"))
              for name in need}
    direct = {name: sites for name, sites in direct.items() if sites}
    if direct:
        for name, sites in direct.items():
            print("ERROR: executable PSP code still calls unresolved", name, "<-", ", ".join(sites))
        return False
    expected = ROOT / "port/psp/expected_stubs.txt"
    if expected.exists():
        approved = {line.strip() for line in expected.read_text().splitlines()
                    if line.strip() and not line.lstrip().startswith("#")}
        unexpected = sorted(set(need) - approved)
        if unexpected:
            print("ERROR: new unresolved functions not in expected_stubs.txt:", ", ".join(unexpected))
            return False
        stale = sorted(approved - set(need))
        if stale:
            print("ERROR: stale symbol(s) remain in expected_stubs.txt:", ", ".join(stale))
            print("Update the reviewed manifest only after explaining their implementation.")
            return False
    lines = ["/* Generated by port/psp/build.py: unresolved PS2 vtable callbacks. */",
             "extern void Port_MissingAt(const char *name, void *caller);"]
    # The caller is obtained inside the actual target stub, before the
    # fatal path. No traversal or unwinding of arbitrary PSP memory.
    lines += [f'__attribute__((noinline)) void {s}(void) {{ '
              f'Port_MissingAt("{s}", __builtin_return_address(0)); }}' for s in need]
    (GEN / "stubs.c").write_text("\n".join(lines) + "\n")
    so = OBJ / "pc_stubs.o"
    subprocess.run([P + "gcc", "-c", "-O1", str(GEN / "stubs.c"), "-o", str(so)], check=True)
    r = subprocess.run(cmd + [str(so)], capture_output=True, text=True)
    errs = [l for l in r.stderr.splitlines() if "multiple definition" in l or "undefined reference" in l or "rror" in l]
    print(f"{len(need)} stubs; link {'OK' if r.returncode == 0 else 'FAILED'}")
    for l in errs[:15]:
        print("  " + l[-200:])
    return r.returncode == 0


def pbp():
    """EBOOT.PBP from the linked program (the PSP's own format: a header, PARAM.SFO and the program)."""
    shutil.copy(ELF, OUT / "bt3.unstripped.elf")
    subprocess.run([P + "strip", "-g", str(ELF)], check=False)
    subprocess.run([str(BIN / "psp-fixup-imports"), str(ELF)], check=True)
    sfo = OUT / "PARAM.SFO"
    # MEMSIZE=1: the program needs the whole user memory of a console with 64 MB (the game heap is 29 MB of it).
    # Without it the system keeps the partition at its 32 MB and the heap cannot be had.
    # (mksfo, pspsdk's other tool, takes no options at all: it would write the file over the next name it is
    # given. mksfoex parses -d properly; the size below catches the day it stops.)
    subprocess.run([str(BIN / "mksfoex"), "-d", "MEMSIZE=1", TITLE, str(sfo)], check=True)
    if not sfo.exists() or sfo.stat().st_size < 300:
        raise SystemExit(f"{sfo}: PARAM.SFO was not written - EBOOT.PBP would boot without MEMSIZE=1")
    args = [str(BIN / "pack-pbp"), str(OUT / "EBOOT.PBP"), str(sfo)]
    for name in ("ICON0.PNG", "ICON1.PMF", "PIC1.PNG", "SND0.AT3"):
        args.append(str(ROOT / "port/psp" / name) if (ROOT / "port/psp" / name).exists() else "NULL")
    args += ["NULL", str(ELF), "NULL"]
    subprocess.run(args, check=True)
    print(f"{OUT / 'EBOOT.PBP'}: {(OUT / 'EBOOT.PBP').stat().st_size} bytes")


if __name__ == "__main__":
    steps = sys.argv[1:] or ["compile", "link", "pbp"]
    ok = True
    if "compile" in steps:
        ok = compile_all()
    if ok and "link" in steps:
        ok = link()
    if ok and "pbp" in steps:
        pbp()
    sys.exit(0 if ok else 1)
