/*
 * PSP: start-up, settings, the log, and the ways out.
 *
 * The game's main() runs on a thread of its own with a large stack (the PS2 code keeps big buffers in locals; the
 * PC builds give it 16 MB). main is wrapped by the linker (--wrap=main), as in the other builds, and so is exit:
 * the port ends the program in several places, and an error is shown on a text screen until START.
 *
 * Files, all under ms0:/PSP/GAME/BT3/ :
 *   disc/              the files of the player's own disc (SLUS_216.78, BIN/DBZP.BIN, DATA/...), as they are on it
 *   saves/             the memory card (port/src/plat_mc.c), bt3_settings.txt the port's settings
 *   bt3.txt            optional, one name=value per line. Read:
 *                        disc=<folder>     another place for the disc files
 *                        wide=0            the console's 4:3 picture in the middle of the screen
 *                        # 29 MB game heap is fixed to preserve PS2 address layout
 *                        report=1          speed figures every two seconds into the log
 *                        controls=legacy|portable  experimental PSP fighting controls
 *                        depth=0|1|2       GE depth test/write: off (default), screen target, all targets
 *                        mode=demo         a computer against computer fight instead of the menus
 *   log.txt            written by every run
 */
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <psppower.h>
#include <pspsysmem.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp_controls.h" /* testable battle-state predicate */

#define HOME "ms0:/PSP/GAME/BT3"
#define GAME_STACK 0x00180000 /* 1.5 MB: the PS2 code's locals (the PC builds give it 16) */

PSP_MODULE_INFO("BT3", PSP_MODULE_USER, 1, 1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
/* PSP-newlib reserves this pool at startup. The previous 42 MiB pool
   left insufficient headroom for kernel-managed thread stacks on some
   PSP/PPSSPP configurations (sceKernelCreateThread: 0x80020190).
   Give 2 MiB back to the system; the PS2-addressed 29 MiB game heap
   below remains UNCHANGED. This leaves ~11 MiB in newlib for transient
   ELF/AFS buffers and texture cache with eviction.
   Requires 64 MiB PSP mode (MEMSIZE=1 in EBOOT PARAM.SFO). */
PSP_HEAP_SIZE_KB(40 * 1024);

char gPspDisc[256] = HOME "/disc";
int gPspWide = 1;          /* the picture filled to the screen's 16:9 (wide=0: 4:3 in the middle) */
int gPortWide = 1;         /* what plat_stub.c Port_AspectMilli reads */
/* A smaller PS2 heap breaks its address layout. Keep this fixed. */
#define PSP_GAME_HEAP_MB 29
/* Experimental presentation pacing: the game has its own 59.94 Hz VBlank
   clock in Port_VBlank. Avoid a SECOND blocking wait in GU frame end.
   Set swap_vsync=1 in bt3.txt to restore the previous physical swap
   wait for PSP hardware/tearing comparison. No game timing is changed. */
int gPspSwapVsync = 0;
/* When swapping buffers, use PSPSDK's returned new drawbuffer address
   rather than guessing by parity. swap_from_sdk=0 restores old behavior. */
int gPspSwapFromSdk = 1;
/* Stage/display experiment, not texture or VU1 changes.
   scene_pair_screen=0 restores the historical routing. */
int gPspScenePairScreen = 1;
/* Preserve paired previous GS frame on the other PSP screen buffer. */
int gPspScenePairSeparate = 1; /* 0 reverts original physical aliasing */
/* Experimental only: last change could make stage black because
   PSP framebuffer readbacks and ownership are not fully implemented. */
int gPspTargetFreshness = 0; /* rollback: latest battle screenshot lost stage background */
/* Experimental top battle HUD sprite depth isolation. PSP uses one Z buffer
   for multiple GS FBPs; 2D overlay must not be occluded by 3D writes.
   hud_top_overlay=0 restores the exact old draw/depth behaviour. */
int gPspHudTopOverlay = 1;
/* Exact GS HUD gauge relies on FRAME.FBMSK (RGB masked, alpha writable),
   FBA and TEST.DATE. Use PSP RGB5551 stencil for the top HUD triangles.
   hud_mask_fix=0 restores legacy rendering for A/B comparison. */
int gPspHudMaskFix = 1;
/* Direct MPEG YCbCr -> PSP RGB5551 output, with a reversible legacy
   RGBA conversion for regression checks via bt3.txt. */
int gPspMovieNative16 = 1;
/* Keep opening/ending MPEG-2 pictures matched to the hardware audio
   timeline when software video decoding cannot sustain 29.97 FPS.
   For visual A/B testing set movie_sync=0 in bt3.txt. */
int gPspMovieSync = 1;
/* PS2 TEST.AREF is in GS alpha units (0x80 = opaque); PSP GU alpha
   samples and tests are in 0..255. Enable correct mask/reference in
   PSP's 2D GS renderer; alpha_fix=0 restores the old behaviour for A/B. */
int gPspAlphaFix = 1;
/* Sprite RGB/A in PS2 GS is flat-shaded from the SECOND XYZ2 vertex,
   unlike a triangle with PRIM.IIP=1. The PSP backend previously let
   sceGu interpolate the first and second sprite vertex colours.
   Disable only for regression comparisons with flat_sprite=0. */
int gPspFlatSprite = 1;
/* Proven visibly better on the fe93677 EBOOT (PR #22): calculate
   GS white-font MODULATE in a separate PSP texture cache variant. */
int gPspWhiteModulate = 1;
/* GS ALPHA.FIX is measured in 1/128 units; PSP GU_FIX is 1/255.
   FIX=0x80 must mean FULL opacity, not half opacity (grey-white UI).
   This conversion is mathematically required, unlike the other GS
   experiments, which remain disabled until visual validation.
   Override using gs_fix=0 only for A/B regression checks. */
int gPspGsFixScale = 1;
int gPspGsClutReuse = 0;
/* PSP experimental build: screen-only depth with Z24->Z16 mapping;
   gs_depth=0 in bt3.txt restores the painter-order baseline. */
int gPspGsDepth = 2;
/* Source texture formats PSMT8H/4HL/4HH: decoder exists but
   opt in before allowing GS passes previously skipped on PSP. */
int gPspGsHiTex = 1; /* enable implemented PSMT8H/4HL/4HH reads; gs_hi_tex=0 rollback */
/* The standalone experimental EBOOT uses balanced PSP 3D texture
   copies by default. No bt3.txt is required; if one exists, tex_limit
   can explicitly select 0 (full original quality), 128, or 256. */
int gPspTexLimit = 256;
/* Native VU1 stage geometry uses Allegrex CPU for conservative strips.
   Falls back to original VU1 for perspective/clipping; other VU1
   programs stay interpreted. Can disable via native_stage4=0. */
int gPspNativeStage4 = 1;
/* Native stage triangles crossing the camera: six-plane CPU clipper,
   with safe VU1 interpreter fallback. native_stage_clip=0 rolls back. */
int gPspNativeStageClip = 1;
/* Skip only mathematically invisible VU1 stage strips; no reduction of
   visible scene geometry, texture size, shading or simulation.
   native_stage_cull=0 restores the original interpreter path. */
int gPspNativeStageCull = 1;
/* Keep exact GS texture/alpha/depth/state for the first stage triangle
   per strip; reuse it for the following triangles with identical UV,
   shade and XYZ packing. native_stage_batch=0 restores old behavior. */
int gPspNativeStageBatch = 1;
/* Native program-0/1 fighter VU1 still used a full GsGpu_Draw()
   for EVERY triangle in both cel and base layers. Reuse first
   triangle's state per layer; 0 restores the original path. */
int gPspNativeFighterBatch = 1;
/* The PSP renderer used to allocate/free 1 MiB RGBA + 256 KiB CLUT
   buffers on EVERY GS texture miss, often evicting GPU textures to
   make room. Reuse startup-allocated decode scratch when possible.
   texture_scratch=0 restores legacy behavior for A/B comparison. */
int gPspTextureScratch = 1;
/* Native two-bone Allegrex skinning + program0 cel lighting/program1
   fade. Only completely accepted strips bypass the original VU1. */
int gPspNativeFighter = 1;
/* GS PSMCT16 colour targets -> PSP 5551 output (not depth, 16S or
   GS VRAM readback). Separate experimental EBOOT from PR #16. */
int gPspGsFb16 = 1;
static FILE *sLog;
static volatile int sEnded;
extern unsigned gPortVBlanks;
extern int gPspReport; /* port/psp/psp_gfx.c */
extern int gPspControlsProfile; /* 0: legacy; 1: portable fight mapping */

/* ---- time ---- */
unsigned long long Psp_TimeUs(void) {
    return (unsigned long long)sceKernelGetSystemTimeWide();
}

/* ---- the log ---- */
/* Every 2-second profiler interval previously called fflush(log.txt) for
   EACH of ~40-50 log lines. This is real synchronous PSP memory-stick
   I/O on both physical PSP and PPSSPP, even while drawing is otherwise
   CPU-bound. Default to ONE flush per report interval (psp_gfx.c);
   force one immediately on startup, fatal errors and exit. Optional
   log_flush=1 preserves per-line flushing for crash investigations. */
int sLogFlush = 0;

void Psp_Log(const char *fmt, ...) {
    char msg[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (sLog != NULL) {
        fputs(msg, sLog);
        if (sLogFlush) {
            fflush(sLog);
        }
    }
}

void Psp_LogFlush(void) {
    if (sLog != NULL) fflush(sLog);
}

/* The renderer's front end, adx.c and eft_c.c log through Vita_Log (the name the Vita port gave it). The PSP
   sends those lines to its own log; on the Vita this function is vita_main.c's. */
void Vita_Log(const char *fmt, ...) {
    char msg[1400];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    Psp_Log("%s", msg);
}

/* ---- buttons ---- */
static unsigned buttons(void) {
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(&pad, 1);
    return pad.Buttons;
}

static unsigned wait_buttons(unsigned mask) {
    while ((buttons() & mask) != 0) { /* released first: the press that brought us here does not count */
        sceKernelDelayThread(20000);
    }
    for (;;) {
        unsigned b = buttons() & mask;
        if (b != 0) {
            return b;
        }
        scePowerTick(0);
        sceKernelDelayThread(20000);
    }
}

static void text_screen(void) {
    pspDebugScreenInit();
    pspDebugScreenClear();
}

static void quit(void) {
    if (sLog != NULL) {
        fflush(sLog);
        fclose(sLog);
        sLog = NULL;
    }
    fflush(stdout);
    fflush(stderr);
    sceKernelExitGame();
    for (;;) {
        sceKernelDelayThread(1000000);
    }
}

/* A message the program cannot go on after: on the screen, in the log, then START ends the program. */
void Psp_Fatal(const char *fmt, ...) {
    char msg[600];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    sEnded = 1;
    Psp_Log("FATAL: %s\n", msg);
    Psp_LogFlush(); /* preserve the fatal log even while waiting for START */
    text_screen();
    pspDebugScreenPrintf("\n%s\n\nPress START to quit.\n", msg);
    wait_buttons(PSP_CTRL_START);
    quit();
}

/* Remaining PS2 middleware callbacks are kept as explicit traps. They
   are referenced by old data tables, and their runtime reachability is
   NOT established. If a callback is executed, capture the MIPS caller
   address so it can be traced to a callsite before implementing it. */
void Port_MissingAt(const char *name, void *caller) {
    Psp_Log("psp: unresolved indirect callback: %s, caller=%p\n", name, caller);
    Psp_Fatal("missing PS2 callback: %s\ncaller=%p\nSee log.txt.", name, caller);
}

void Port_Missing(const char *name) {
    Port_MissingAt(name, NULL);
}

/* Battle status access is in psp_battle_state.c, which can include the game's
 * BattleWork declaration without colliding with PSP SDK typedefs. */

/* exit(), from anywhere in the port or the game: keep the screen until it has been read. */
void __wrap_exit(int code);

/* ---- the game's thread ---- */
extern int __real_main(int argc, char **argv);

static int game_thread(SceSize args, void *argp) {
    static char *argv[] = {"bt3", NULL};

    (void)args;
    (void)argp;
    exit(__real_main(1, argv));
    return 0;
}

void __wrap_exit(int code) {
    if (!sEnded) {
        sEnded = 1;
        fflush(stdout);
        fflush(stderr);
        text_screen();
        Psp_Log("psp: exit(%d) at vertical blank %u\n", code, gPortVBlanks);
        Psp_LogFlush();
        pspDebugScreenPrintf("\nThe program ended with code %d.\n", code);
        pspDebugScreenPrintf("\nThe log is " HOME "/log.txt. Press START to quit.\n");
        wait_buttons(PSP_CTRL_START);
    }
    quit();
    for (;;) {
    }
}

/* ---- settings ---- */
/* bt3.txt: name=value lines; '#' starts a comment. */
static void setting(const char *name, const char *value) {
    if (strcmp(name, "disc") == 0 && value[0] != '\0') {
        snprintf(gPspDisc, sizeof(gPspDisc), "%s", value);
    } else if (strcmp(name, "controls") == 0) {
        /* Portable is the embedded default; legacy remains an explicit rollback. */
        if (strcmp(value, "portable") == 0) gPspControlsProfile = 1;
        else if (strcmp(value, "legacy") == 0) gPspControlsProfile = 0;
        else Psp_Log("psp: invalid controls=%s (expected legacy or portable)\n", value);
    } else if (strcmp(name, "wide") == 0) {
        gPspWide = atoi(value) != 0;
    } else if (strcmp(name, "swap_vsync") == 0) {
        gPspSwapVsync = atoi(value) != 0;
    } else if (strcmp(name, "swap_from_sdk") == 0) {
        gPspSwapFromSdk = atoi(value) != 0;
    } else if (strcmp(name, "scene_pair_screen") == 0) {
        gPspScenePairScreen = atoi(value) != 0;
    } else if (strcmp(name, "scene_pair_separate") == 0) {
        gPspScenePairSeparate = atoi(value) != 0;
    } else if (strcmp(name, "target_freshness") == 0) {
        gPspTargetFreshness = atoi(value) != 0;
    } else if (strcmp(name, "hud_top_overlay") == 0) {
        gPspHudTopOverlay = atoi(value) != 0;
    } else if (strcmp(name, "hud_mask_fix") == 0) {
        gPspHudMaskFix = atoi(value) != 0;
    } else if (strcmp(name, "movie_native16") == 0) {
        gPspMovieNative16 = atoi(value) != 0;
    } else if (strcmp(name, "movie_sync") == 0) {
        gPspMovieSync = atoi(value) != 0;
    } else if (strcmp(name, "alpha_fix") == 0) {
        gPspAlphaFix = atoi(value) != 0;
    } else if (strcmp(name, "flat_sprite") == 0) {
        gPspFlatSprite = atoi(value) != 0;
    } else if (strcmp(name, "white_sprite") == 0) {
        gPspWhiteModulate = atoi(value) != 0;
    } else if (strcmp(name, "gs_fix") == 0) {
        gPspGsFixScale = atoi(value) != 0;
    } else if (strcmp(name, "gs_clut") == 0) {
        gPspGsClutReuse = atoi(value) != 0;
    } else if (strcmp(name, "gs_depth") == 0) {
        int m = atoi(value);
        gPspGsDepth = m < 0 ? 0 : m > 2 ? 2 : m; /* 0 off, 1 all targets, 2 screen only */
    } else if (strcmp(name, "gs_hi_tex") == 0) {
        gPspGsHiTex = atoi(value) != 0;
    } else if (strcmp(name, "tex_limit") == 0) {
        int cap = atoi(value);
        gPspTexLimit = (cap == 128 || cap == 256) ? cap : 0;
    } else if (strcmp(name, "native_stage4") == 0) {
        gPspNativeStage4 = atoi(value) != 0;
    } else if (strcmp(name, "native_stage_clip") == 0) {
        gPspNativeStageClip = atoi(value) != 0;
    } else if (strcmp(name, "native_stage_cull") == 0) {
        gPspNativeStageCull = atoi(value) != 0;
    } else if (strcmp(name, "native_stage_batch") == 0) {
        gPspNativeStageBatch = atoi(value) != 0;
    } else if (strcmp(name, "native_fighter_batch") == 0) {
        gPspNativeFighterBatch = atoi(value) != 0;
    } else if (strcmp(name, "texture_scratch") == 0) {
        gPspTextureScratch = atoi(value) != 0;
    } else if (strcmp(name, "log_flush") == 0) {
        sLogFlush = atoi(value) != 0;
    } else if (strcmp(name, "native_fighter") == 0) {
        gPspNativeFighter = atoi(value) != 0;
    } else if (strcmp(name, "gs_fb16") == 0) {
        gPspGsFb16 = atoi(value) != 0;
    } else if (strcmp(name, "heap") == 0) {
        /* Not configurable: smaller heaps can corrupt PS2 pointers.
           Do not print an untruthful size, warn once per setting. */
        Psp_Log("psp: ignored unsupported heap=%s (fixed at 29 MB)\n", value);
    } else if (strcmp(name, "report") == 0) {
        gPspReport = atoi(value) != 0;
    } else if (strcmp(name, "mode") == 0 && strcmp(value, "demo") == 0) {
        setenv("BT3_DEMO", "1", 1);
    } else if (strcmp(name, "demopick") == 0) {
        setenv("BT3_DEMO_PICK", value, 1);
    } else if (strcmp(name, "demostage") == 0) {
        setenv("BT3_DEMO_STAGE", value, 1);
    } else if (strcmp(name, "demochara1") == 0 || strcmp(name, "demochara2") == 0) {
        char *end = NULL;
        long id = strtol(value, &end, 10);
        if (value != end && *end == '\0' && id >= 0 && id <= 255) {
            setenv(strcmp(name, "demochara1") == 0
                   ? "BT3_DEMO_CHARA1" : "BT3_DEMO_CHARA2", value, 1);
        } else {
            Psp_Log("psp: ignoring invalid %s=%s\n", name, value);
        }
    }
}

static void read_settings(void) {
    FILE *fp = fopen(HOME "/bt3.txt", "r");
    char line[400];

    if (fp == NULL) {
        Psp_Log("psp: no bt3.txt in " HOME "; embedded defaults apply\n");
        return;
    }
    Psp_Log("psp: reading settings from " HOME "/bt3.txt\n");
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *h, *n = line, *v;
        if ((h = strchr(line, '#')) != NULL) {
            *h = '\0';
        }
        v = strchr(n, '=');
        if (v == NULL) {
            continue;
        }
        *v++ = '\0';
        while (*n == ' ' || *n == '\t') { n++; }
        {   /* the value without its spaces and its end of line */
            size_t k = strlen(v);
            while (k > 0 && (v[k - 1] == '\n' || v[k - 1] == '\r' || v[k - 1] == ' ' || v[k - 1] == '\t')) { v[--k] = '\0'; }
        }
        setting(n, v);
    }
    fclose(fp);
}

/* ---- the ways out (the PSP's own) ---- */
static int exit_callback(int arg1, int arg2, void *common) {
    (void)arg1; (void)arg2; (void)common;
    exit(9);
    return 0;
}

static int callback_thread(SceSize args, void *argp) {
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static int setup_callbacks(void) {
    SceUID th = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, PSP_THREAD_ATTR_USER, 0);
    if (th >= 0) {
        sceKernelStartThread(th, 0, NULL);
    }
    return th;
}

/* These SDK partition totals can differ from newlib/user-heap
   availability. They are diagnostics, not proof of free game RAM. */
static void log_thread_memory(const char *phase) {
    Psp_Log("psp: thread memory %s: partition-total=%u KB partition-largest=%u KB game-stack=%u KB newlib-pool=%u MB game-heap=%u MB\n",
            phase, (unsigned)(sceKernelTotalFreeMemSize() / 1024u),
            (unsigned)(sceKernelMaxFreeMemSize() / 1024u),
            (unsigned)(GAME_STACK / 1024u), 40u, (unsigned)PSP_GAME_HEAP_MB);
}

/* ---- start ---- */
extern void Psp_MapMemory(void);
extern void Psp_LoadGameData(const char *disc);
extern int Psp_GuInit(void);

int __wrap_main(int argc, char **argv) {
    SceUID th;

    (void)argc;
    (void)argv;
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166); /* the console's own top speed: no overclocking */
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    pspDebugScreenInit();
    sceIoMkdir(HOME, 0777);
    sLog = fopen(HOME "/log.txt", "w");
    Psp_Log("======== new run ========\n");
    Psp_Log("psp: Dragon Ball Z BT3 (Tenkaichi3Decomp) for the PSP, built " __DATE__ " " __TIME__ "\n");
    Psp_Log("psp: BUILD-ID TP3-PORTABLE-DEFAULT-20261026\n");

    setenv("BT3_DATA", HOME, 1);
    setenv("BT3_SAVES", HOME "/saves", 1);
    setenv("BT3_SETTINGS", HOME "/bt3_settings.txt", 1);
    read_settings();
    gPortWide = gPspWide;

    Psp_Log("psp: disc folder %s, heap %d MB, report=%d\n",
            gPspDisc, PSP_GAME_HEAP_MB, gPspReport);
    Psp_Log("psp: controls=%s (portable active only in unpaused live fights)\n",
            gPspControlsProfile == 1 ? "portable" : "legacy");
    Psp_Log("psp: swap_vsync=%d (0: game VBlank pacing only; 1: extra GU display wait)\n",
            gPspSwapVsync);
    Psp_Log("psp: movie_native16=%d (0: legacy RGBA path; 1: direct RGB5551 path)\n",
            gPspMovieNative16);
    Psp_Log("psp: movie_sync=%d (0: decode all MPEG frames, 1: skip frames late versus audio)\n",
            gPspMovieSync);
    Psp_Log("psp: alpha_fix=%d (1: GS alpha mask/reference fix, 0: legacy GU alpha test)\n",
            gPspAlphaFix);
    Psp_Log("psp: flat_sprite=%d (1: GS flat color for sprites/non-IIP, 0: previous GU interpolation)\n",
            gPspFlatSprite);
    Psp_Log("psp: white_sprite=%d (PR22 fe93677 GS text /128 RGB; separate cache)\n",
            gPspWhiteModulate);
    Psp_Log("psp: GS alpha FIX conversion=%d (1: GS 0x80 -> PSP GU 0xFF; 0: legacy half-intensity)\n",
            gPspGsFixScale);
    Psp_Log("psp: GS A/B experiments: gs_fix=%d gs_clut=%d gs_depth=%d (0 off, 1 all targets, 2 screen only) gs_hi_tex=%d (only gs_fix defaults 1)\n",
            gPspGsFixScale, gPspGsClutReuse, gPspGsDepth, gPspGsHiTex);
    Psp_Log("psp: texture LOD: tex_limit=%d (0: original; 256: balanced; 128: lighter, 3D polygons only)\n",
            gPspTexLimit);
    Psp_Log("psp: GS paired framebuffer separate physical backing=%d (0: previous shared behavior)\n",
            gPspScenePairSeparate);
    Psp_Log("psp: native stage VU1 program4=%d (automatic conservative fallback)\n",
            gPspNativeStage4);
    Psp_Log("psp: native fighter VU1 programs0/1=%d (bone animation, lighting; interpreter on rejected batches)\n",
            gPspNativeFighter);
    Psp_Log("psp: GS FB16 colour translation=%d (PSMCT16 only, GU5551; no depth/readback)\n",
            gPspGsFb16);
    /* Reserve kernel-managed 1.5 MiB game thread stack while memory
       is still contiguous. Starting this thread is intentionally
       deferred until game data, heap, and GU are ready. */
    log_thread_memory("before-create");
    th = sceKernelCreateThread("bt3_game", game_thread, 0x20,
                               GAME_STACK, PSP_THREAD_ATTR_USER, 0);
    if (th < 0) {
        log_thread_memory("create-failed");
        Psp_Fatal("PSP memory too low for 1.5 MB game thread (0x%08X).\n"
                  "Check PSP 64 MB memory mode. See log.txt.\n"
                  "No bt3.txt is required.", (unsigned)th);
    }
    log_thread_memory("reserved");
    pspDebugScreenPrintf("BT3 for the PSP\n\nReading the game's data tables from\n%s ...\n", gPspDisc);
    Psp_LoadGameData(gPspDisc);
    Psp_MapMemory();
    /* Boot status is only needed while reading the disc. Erase the
       debugger's text before handing the screen to sceGu; failures
       below still reinitialize the debug screen via Psp_Fatal(). */
    pspDebugScreenClear();
    if (!Psp_GuInit()) {
        Psp_Fatal("the graphics unit could not be started");
    }
    Psp_LogFlush(); /* boot diagnostics durable before gameplay begins */
    Psp_Log("psp: starting the game (reserved thread=%d)\n", (int)th);
    log_thread_memory("before-start");
    {
        int started = sceKernelStartThread(th, 0, NULL);
        if (started < 0) {
            /* Previously logged th (a successful handle), not the actual
               start failure. Keep the error actionable on Android PPSSPP. */
            Psp_Fatal("cannot START the game's thread (error 0x%08X)", (unsigned)started);
        }
    }
    sceKernelWaitThreadEnd(th, NULL);
    for (;;) {
        sceKernelDelayThread(1000000);
    }
    return 0;
}
