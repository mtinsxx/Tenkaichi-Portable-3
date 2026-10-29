/*
 * PSP: start-up, settings, the log, and the ways out.
 *
 * The game's main() runs on a thread of its own with a large stack (the PS2 code keeps big buffers in locals; the
 * PC builds give it 16 MB). main is wrapped by the linker (--wrap=main), as in the other builds, and so is exit:
 * the port ends the program in several places, and an error is shown on a text screen until START.
 *
 * Files in ms0:/PSP/GAME/BT3/:
 *   EBOOT.PBP         native PSP application
 *   disc/             original game files from the owner's copy
 *   saves/            in-game memory card
 *   bt3_settings.txt  game's own settings (not an EBOOT config file)
 *   log.txt           automatically written on each run
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
   An alternate physical swap policy requires recompilation. No game timing is changed. */
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
   RGBA converter kept in the code for development builds. */
int gPspMovieNative16 = 1;
/* Keep opening/ending MPEG-2 pictures matched to the hardware audio
   timeline when software video decoding cannot sustain 29.97 FPS.
   Alternate synchronization requires a developer rebuild. */
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
   Keep depth enabled; disabling it caused visibly broken character models. */
int gPspGsDepth = 2;
/* Source texture formats PSMT8H/4HL/4HH: decoder exists but
   opt in before allowing GS passes previously skipped on PSP. */
int gPspGsHiTex = 1; /* enable implemented PSMT8H/4HL/4HH reads; gs_hi_tex=0 rollback */
/* The standalone experimental EBOOT uses balanced PSP 3D texture
   copies by default. The default is embedded and cannot be overridden by a text file. */
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
/* PSP's newlib FILE buffer is written by the game, sound and renderer
   threads. Serialize their writes and flushes: the 2026-10-27 battle
   trace contained a 415098-byte NUL hole between buffered reports.
   This is a hardening precaution, not proof of why the hole occurred. */
static SceUID sLogLock = -1;
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
   flushing is embedded for this diagnostic EBOOT. */
int sLogFlush = 0;
/* Bounded full renderer diagnostics: permanently embedded in this diagnostic EBOOT. */
int gPspLogAll = 1;

static int Psp_LogAcquire(void) {
    return sLogLock >= 0 ? sceKernelWaitSema(sLogLock, 1, NULL) >= 0 : 0;
}
static void Psp_LogRelease(int acquired) {
    if (acquired) sceKernelSignalSema(sLogLock, 1);
}
void Psp_Log(const char *fmt, ...) {
    char msg[1024];
    va_list ap;
    int held;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    held = Psp_LogAcquire();
    if (sLog != NULL) {
        fputs(msg, sLog);
        if (sLogFlush) fflush(sLog);
    }
    Psp_LogRelease(held);
}

void Psp_LogFlush(void) {
    int held = Psp_LogAcquire();
    if (sLog != NULL) fflush(sLog);
    Psp_LogRelease(held);
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
    int held = Psp_LogAcquire();
    if (sLog != NULL) {
        fflush(sLog);
        fclose(sLog);
        sLog = NULL;
    }
    Psp_LogRelease(held);
    if (sLogLock >= 0) {
        sceKernelDeleteSema(sLogLock);
        sLogLock = -1;
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

/* All PSP controls, rendering and diagnostics use EBOOT-native defaults.
 * No bt3.txt file is ever opened; the separate bt3_settings.txt belongs
 * to the game's own saved settings and is untouched. */
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
    sLogLock = sceKernelCreateSema("tp3_log_mutex", 0, 1, 1, NULL);
    sLog = fopen(HOME "/log.txt", "w");
    Psp_Log("======== new run ========\n");
    Psp_Log("psp: Dragon Ball Z BT3 (Tenkaichi3Decomp) for the PSP, built " __DATE__ " " __TIME__ "\n");
    Psp_Log("psp: BUILD-ID TP3-NATIVE-T8-CONVERTER-20261029\n");
    Psp_Log("psp: multi-thread LOG_ALL mutex=%s (no config file)\n",
            sLogLock >= 0 ? "on" : "unavailable");

    setenv("BT3_DATA", HOME, 1);
    setenv("BT3_SAVES", HOME "/saves", 1);
    setenv("BT3_SETTINGS", HOME "/bt3_settings.txt", 1);
    /* Diagnostic, depth and portable controls are built into the EBOOT.
       No external bt3.txt is read and no user action is required. */
    gPortWide = gPspWide;
    Psp_Log("psp: LOG_ALL built into EBOOT diagnostics=%d report=%d flush-per-line=%d external-config=disabled\n",
            gPspLogAll, gPspReport, sLogFlush);

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
                  "LOG_ALL is built into EBOOT.PBP.", (unsigned)th);
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
