/*
 * PSP: the Graphics Synthesizer's back end on the PSP's graphics engine (sceGu).
 *
 * gs_core.c decodes what the game sends and keeps GS memory; this file takes over at "a primitive with the current
 * GS registers" (GsGpu_Draw). Everything is drawn as it arrives, in the PS2's own order:
 *
 *   targets        one 480 x 272 16-bit target per frame-buffer address the game draws into (two are the game's
 *                  own buffers, the rest are the work buffers of its screen effects), each with the depth buffer
 *                  of the frame; they live in the PSP's 2 MB of graphics memory, together with the textures
 *   textures       GS memory decoded into swizzled PSP textures (16-bit, or 32-bit where the alpha needs it),
 *                  cached by TEX0 / TEXA and the content hash of the pages they occupy
 *   blending       the GS equation ((A - B) * C >> 7) + D mapped onto the engine's blend factors
 *   coordinates    the game's frame-buffer pixels are scaled to the PSP's 480 x 272 screen
 *
 * Not drawn (the PS2's memory layout is what makes them possible, and the PSP has no working buffer of that size
 * for them): a 16-bit view of a frame buffer, passes that draw into the depth buffer's memory, and the depth
 * buffer's spare byte sampled as a palette. What the game would have put in a work buffer is still copied there
 * when the game uploads into it (GsGpu_FbUpload), so the fades that read it back are right.
 *
 * The VU1 programs are interpreted by gs_vu1.c (BT3_VU_INTERP): the PSP has no vertex shaders, and the
 * interpreter is the reference the native versions would have to be checked against.
 */
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gs_internal.h"
#include "gs_experiments.h"
#include "gs_white_modulate.h"
#include "psp_clut_hash.h"
#include "gs_hi_tex.h"
#include "texture_lod.h"
#include "psp_stage4.h"
#include "psp_stage4_clip.h"
#include "psp_gu_swap_check.h"
#include "psp_scene_pair.h"
#include "psp_hud_overlay.h"
#include "psp_vu_profile.h"
#include "psp_hud_mask.h"
#include "psp_fighter_vu1.h"
#include "psp_gs_fb16.h"
#include "psp_gs_depth_z.h"
#include "psp_tex_reuse.h"
#include "psp_tex_lifetime.h"

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512            /* the frame buffers' stride: a power of two the engine wants */
#define FB_SIZE (BUF_W * SCR_H * 2)
#define DEPTH_SIZE (BUF_W * SCR_H * 2)

/* Where everything sits in the PSP's graphics memory (2 MB). */
#define VRAM_BASE ((void *)0x04000000u) /* uncached (the processor reads it back for the fades) */
#define FB0_OFF 0
#define FB1_OFF FB_SIZE
#define DEPTH_OFF (2 * FB_SIZE)
/* PS2 auxiliary render targets must not overwrite the active PSP screen.
   Reserve one 512x272 5551 working framebuffer from the spare VRAM.
   (Multiple simultaneous PS2 effect targets still need a more complete
   solution; this keeps the main picture isolated.) */
#define WORK_OFF (DEPTH_OFF + DEPTH_SIZE)
#define WORK_SIZE FB_SIZE
#define TEX_OFF (WORK_OFF + WORK_SIZE)
#define TEX_SIZE (0x200000u - TEX_OFF)

/* Guard the PSP's physical 2 MiB VRAM map at compile time. The three
   screen/depth surfaces and the one work surface must not overlap the
   texture region, regardless of later tuning. */
#if FB1_OFF < (FB0_OFF + FB_SIZE) || DEPTH_OFF < (FB1_OFF + FB_SIZE) || WORK_OFF < (DEPTH_OFF + DEPTH_SIZE) || TEX_OFF < (WORK_OFF + WORK_SIZE)
#error "PSP GU frame/depth/work regions overlap"
#endif
#if TEX_OFF >= 0x200000u || TEX_SIZE < 4096u
#error "PSP GU texture region is outside physical VRAM"
#endif

#define MAX_TEX 384
#define MAX_TEX_BYTES (TEX_SIZE - 4 * 1024)
/* More textures remain decoded in GE-readable main RAM when VRAM is full. */
#define MAX_RAM_TEX_BYTES (10u * 1024u * 1024u)
#define MAX_QUEUE 8192       /* vertices of one batch */
#define MAX_TARGETS 6

/* MPEG-2 movie (512x448 RGBA) -> small, linear 512x256 5551 texture in
   cached main RAM (not VRAM: framebuffers and the game texture cache must
   not be overwritten). The displayed area is 480x256 texels, stretched
   to the PSP's 480x272 viewport, consistent with the port's 2D scaling. */
#define MOVIE_SRC_W 512
#define MOVIE_SRC_H 448
#define MOVIE_TEX_W 512
#define MOVIE_TEX_H 256
#define MOVIE_DISPLAY_W 480

typedef struct Vtx {         /* what the engine reads: texture coordinates, colour, position */
    float u, v;
    uint32_t c;
    float x, y, z;
} Vtx;

#include "psp_stage_tri_batch.h"

typedef struct Tex {
    uint64_t tex0, texa;
    uint32_t gen;
    void *ptr;               /* VRAM or aligned main RAM, readable by GE */
    void *heap_alloc;        /* malloc base when stored in main RAM */
    unsigned w, h, bytes;
    int fmt;                 /* GU_PSM_5551 / GU_PSM_8888 */
    int swizzled;            /* whether this texture uses the PSP's tiled layout */
    int white_sprite;        /* independent white-modulation texture variant */
    unsigned last;           /* frame last used */
} Tex;

typedef struct Target {
    uint32_t fbp;
    void *color, *depth;
    unsigned last;
    uint32_t gen;  /* last GS upload generation when this FBP was rendered */
    int rendered;  /* a GU draw has written to this particular backing buffer */
    int used;
} Target;

static unsigned int __attribute__((aligned(64))) sList[65536];
static unsigned char *sVram = (unsigned char *)VRAM_BASE;
static unsigned sTexMem;     /* bytes of TEX_SIZE given out */
static unsigned sTexRamMem, sTexRamPeak;
/* Reusable decode buffers remove thousands of alloc/free attempts
   and corresponding GPU-fenced cache evictions under battle pressure.
   A/B: texture_scratch=0 in optional bt3.txt. */
static uint32_t *sScratchRgba;
static uint8_t *sScratchIndices;
static unsigned sScratchReservedBytes, sTexRamBudget = MAX_RAM_TEX_BYTES;
static unsigned sScratchRgbaUses, sScratchIndicesUses;
/* Workload proof: count GPU waits genuinely needed for current-frame
   texture eviction separately from old-frame textures already fenced. */
static unsigned sTexEvictAfterFrame, sTexEvictSameFrame;
static unsigned long long sTexEvictFenceUs;
static unsigned sTexCacheHits, sTexCacheEvicted, sTexCacheMisses;
static unsigned sTexHeapFailed, sTexOversized;
/* Report only successful reduced copies, and 3D cache reuses. */
static unsigned sLodReduced, sLodCacheHits;
static unsigned long long sLodPixelsSaved;
/* Pressure diagnostics: allocations can fail before the nominal 10 MiB
   texture-cache ceiling because the game/PS2 interpreter shares PSP RAM. */
static unsigned sTexPressureEvictions, sTexRecoveredAllocations;
static unsigned sTexScratchFailures, sTexScratchRecovered;
static int sTexSwizzled;     /* layout of the last cache-hit/new texture */
static Tex sTex[MAX_TEX];
static int sTexCount;
static PspTexReuse sTexReuse[8];
static uint32_t sTexSerial = 1u;
static unsigned sTexReuseHits, sTexReuseFullKeys;
static Target sTargets[MAX_TARGETS];
static Vtx *sVb;             /* the vertex batch: ordinary memory, the engine reads it from there */
static int sVbCount, sVbMode; /* count, and what primitives the batch holds */
static int sReady;
static unsigned sFrame;
static unsigned sDraws, sSkipped, sNewTex, sBatches;
/* PSP-native stage (VU1 program 4), without replacing the game/GS logic. */
static PspStage4Vertex sStage4Prepared[1024];
static PspStageClipVertex sStage4ClipSource[1024];
static unsigned sStage4ClipFlag[1024];
static uint32_t sStage4PreparedCount;
static unsigned sNativeStageClipTriangles, sNativeStageClipOutside, sNativeStageClipFailed;
static unsigned sNativeStageBatches, sNativeStageVerts, sNativeStageFallback;
/* Native program-4 triangle GS batching: the original GsGpu_Draw
   still handles the first triangle of each strip, subsequent triangles
   reuse its texture/target/state and emit identical GU vertices. */
static unsigned sNativeStageFastStarts, sNativeStageFastTriangles;
/* Same no-state-change optimization for the two fighter VU1 layers:
   each layer has one TEX0/PRIM; reuse the first triangle's proven state. */
static unsigned sNativeFighterFastStarts, sNativeFighterFastTriangles;
/* Number of full strips provably clipped before the VU1 interpreter.
   No visible triangles can be removed by the conservative proof. */
static unsigned sNativeStageCulledStrips, sNativeStageCulledVerts;
static int sStage4Culled;
/* Program-0/1 fighter strips are capped at 38 vertices by the actual
   PS2 dual-layer GIF packet size. No per-draw heap allocations. */
static PspFighterVertex sFighterPrepared[38];
static uint32_t sFighterPreparedCount;
static int sFighterPreparedFade;
static unsigned sFighterNativeBatches, sFighterNativeVerts, sFighterNativeFallback;
/* Per-report counters, not accumulated from startup. These distinguish
   an alpha-composition problem from a dropped GS pass / framebuffer
   routing problem in the actual menu scene. */
static unsigned sDiagSkip16, sDiagSkipDepth, sDiagSkipPsm;
static unsigned sDiagFb16Native;
static unsigned sDiagSkipTexMem, sDiagSkipNoTarget;
static unsigned sDiagHiTexBlocked, sDiagHiTexAllowed, sDiagHiTexDecodeFailed;
static unsigned sDiagDrawMain, sDiagDrawWork;
static unsigned sDiagAlphaKeep, sDiagAlphaOther, sDiagAlphaRefNonzero;
static unsigned sDiagAlphaOtherMode[4];
static unsigned sDiagFlatSprites, sDiagUnequalSpriteColors;
static unsigned sHudTopSpriteCount, sHudTopDepthSuppressed;
static unsigned sHudTopDepthRequested, sHudTopDepthWriteRequested;
static unsigned sHudTopTfxModes[4], sHudTopClampModes[4];
static unsigned sHudMaskClear, sHudMaskSet, sHudMaskDateZero, sHudMaskDateOne;
/* Hud_Draw calls HudGfx_ClearAlpha() BEFORE EACH player's gauge:
   it is sixteen full-height 32-pixel GS sprites, previously ignored. */
static unsigned sHudMaskFullClearStrips, sHudMaskFullClearSweeps;
static unsigned sHudMaskDateOther, sHudMaskOutside;
/* GS CLAMP can select REGIONAL clamp/repeat. PSP GU only supports
   full-texture wrap/clamp, so log how often each unsupported mode is
   needed before changing UV geometry or texture atlases. */
static unsigned sDiagClampS[4], sDiagClampT[4], sDiagTfx[4];
static unsigned sDiagTccRgb, sDiagBlendOn;
/* Count unsupported GS features without changing any pixels (P03/P06/P07).
   Logs are aggregated once per report interval, never per draw. */
static unsigned sDiagFixUsed, sDiagFixRange[4], sDiagClutReuseCandidate;
static unsigned sDiagSpriteFixUsed, sDiagSpriteFixOpaque, sDiagSpriteHighlight;
static unsigned sDiagSpriteDecalRgba, sDiagSpriteDecalRgb;
static unsigned sDiagWhiteSpriteDraws, sDiagWhiteTextureHits, sDiagWhiteTextureBuilds;
/* Indexed CLUT hash on every draw was 16/256 independent swizzled
   pixel lookups even on a texture-cache hit. Decode the same 16xN
   palette through the already-optimized GS block rectangle reader.
   Sample the original slow hash to catch format/layout regressions. */
static unsigned sClutFastCalls, sClutSlowSamples, sClutHashMismatches;
static int sClutRectSafe = 1;
static unsigned sDiagZTestRequested, sDiagZWriteRequested;
/* Hardware-dependent calls that are not yet functionally ported.
   Count actual runtime use before attempting an unsafe FB copy. */
static unsigned sDiagUnimplementedFbUpload, sDiagUnimplementedNativeEffect;
/* Depth (TEST.ZTE/ZTST, ZBUF.ZMSK) of the primitives actually reached.
   The approved baseline ignores all of it (see state_apply), so these
   numbers say how much of a scene is being drawn painter's-order that
   the GS would have depth-tested, and whether its Z values vary at all.
   Triangles are counted separately from sprites: fighters/stages are
   triangles, HUD/menus are almost all sprites. */
static unsigned sDiagZTri, sDiagZTriTest, sDiagZTriWrite, sDiagZTriWork;
static unsigned sDiagZTriFlat; /* triangles whose 3 vertices share one Z */
static unsigned sDiagZFunc[4]; /* TEST.ZTST of depth-tested triangles */
static unsigned sDiagZSprite, sDiagZSpriteTest;
static uint32_t sDiagZMin = 0xFFFFFFFFu, sDiagZMax;

/* Low-overhead wall-clock profiler. Intervals are reported by Psp_VBlank()
   every ~2 seconds, not per draw. This does not bypass any rendering path. */
static unsigned long long sProfGameNs, sProfChainNs;
static unsigned long long sProfKeySampleUs, sProfDecodeUs;
static unsigned long long sProfGeSyncUs, sProfVblankUs;
static unsigned sProfChainCount, sProfKeyCalls, sProfKeySamples;
static unsigned sProfDecodeCount, sProfFrameEnds;
/* Low-overhead CPU sample of the PSP renderer's GS draw submissions.
   Split renderer time from the surrounding GIF/VIF command interpreter. */
static unsigned sProfDrawCalls, sProfDrawSamples;
static unsigned long long sProfDrawSampleUs;

void Psp_GuProfileChainNs(unsigned long long ns) {
    sProfChainNs += ns;
    ++sProfChainCount;
}

void Psp_GuProfileGameNs(unsigned long long ns) {
    sProfGameNs += ns;
}

static unsigned sMainDrawOffset = FB0_OFF; /* synchronized against GU's returned new drawbuffer */
static unsigned sGuSwapMismatch, sGuSwapUnknown;
static unsigned sFrameStageInputs, sFrameFighterInputs;
static unsigned sFrameScreenDraws, sFrameWorkDraws, sFrameWorkOwners;
static unsigned sFrameDiagShown, sFrameLastDiag;
static unsigned sTargetsRegistered;
/* Only one real auxiliary PSP framebuffer exists; logical GS FBP targets
   that share it cannot all be considered valid at the same time. */
static uint32_t sWorkOwnerFbp = UINT32_MAX;
static unsigned sFbAliasHit, sFbAliasFallback, sFbAliasReupload;
static unsigned sFbAliasUnrendered, sFbAliasSelf, sFbWorkOwnerChanges;
extern int gGsMainFbp; /* gs_sony.c: the frame buffer the frame is drawn into */
extern int gGsDisplayOtherFbp; /* libgraph's paired SCREEN buffer */
extern int gPspScenePairScreen;
static unsigned sPairedDisplayRouted, sPairedDisplayFrames, sPairedThisFrame;
static unsigned sPairedScreenDraws, sPairedWorkDraws;
static unsigned gPspGuFlushes;

/* A movie frame remains valid in plat_movie.c until the next MPEG decode.
   Keep a static 256 KiB staging texture outside the PSP's scarce 2 MiB
   VRAM. It is written only on the game thread before GU submission. */
static const unsigned char *sMovieRgba;
/* Optional direct RGB5551 MPEG path: same pixels, no intermediate RGBA. */
static const uint16_t *sMovieDirect5551;
static uint16_t sMoviePixels[MOVIE_TEX_W * MOVIE_TEX_H] __attribute__((aligned(64)));
static Vtx sMovieVertices[2] __attribute__((aligned(16)));
static unsigned sMovieSubmitted, sMovieDisplayed;
static unsigned long long sMovieConvertTotalUs;
static unsigned sMovieConvertMaxUs;
extern unsigned long long Psp_TimeUs(void);
extern void Psp_Log(const char *fmt, ...);
extern int gPspSwapVsync;
extern int gPspSwapFromSdk;
extern int gPspAlphaFix;
extern int gPspFlatSprite;
extern int gPspWhiteModulate;
extern int gPspGsFixScale;
extern int gPspGsClutReuse;
extern int gPspGsDepth;
extern int gPspGsHiTex;
extern int gPspTexLimit;
extern int gPspNativeStage4;
extern int gPspNativeStageBatch;
extern int gPspNativeFighterBatch;
extern int gPspNativeFighter;
extern int gPspHudTopOverlay;
extern int gPspHudMaskFix;
extern int gPspGsFb16;
extern int gPspTextureScratch;

/* ---- the state the engine is in (only what changed is sent again) ---- */
static struct {
    void *tex;
    int tw, th, fmt, tmode;
    int tfx, tcc, wrapu, wrapv, filt;
    int blendop, src, dst, sfix, dfix, blend_on;
    int atest, afunc, aref, amask;
    int ztest, zfunc, zwrite;
    int scx, scy, scw, sch;
    unsigned pixelMask;
    int stencilMode;
    void *target;
} sSt;

/* Last state actually submitted to the PSP GE, distinct from the game's
   current GS state (sSt). The GS batcher already flushes on a change,
   but previously resent EVERY sceGu* register for EVERY batch — even if
   only its texture changed. Vita similarly keeps its applied GPU state.
   A freshly started frame/list explicitly invalidates this snapshot. */
static __typeof__(sSt) sApplied;
static int sAppliedValid;
static unsigned sGuStateCalls, sGuStateChanges, sGuTargetChanges;


/* ------------------------------------------------------------------ small helpers */
static void flush(void);

unsigned Psp_GuVramFree(void) { return MAX_TEX_BYTES - sTexMem; }

/* PSP GU swizzle: 16 bytes x 8 rows per 128-byte block.
   The old implementation recalculated the tiled destination offset and
   copied ONE byte at a time.  For a 512x512 32-bit texture that meant
   over one million address calculations.  Copy each of the eight 16-byte
   rows directly instead. This is bit-for-bit identical for the power-of-2
   textures the PSP backend already permits (rowBytes % 16 == 0, h % 8 == 0).
   Tiny textures remain linear and never call this function. */
static void swizzle(unsigned char *dst, const unsigned char *src, unsigned w, unsigned h, int bpp) {
    unsigned rowBytes = w * (unsigned)bpp, x, y, row;
    for (y = 0; y < h; y += 8) {
        for (x = 0; x < rowBytes; x += 16) {
            unsigned char *d = dst + ((y / 8) * (rowBytes / 16) + x / 16) * 128;
            const unsigned char *p = src + y * rowBytes + x;
            for (row = 0; row < 8; ++row) {
                memcpy(d + row * 16, p + row * rowBytes, 16);
            }
        }
    }
}

/* The GS's alpha: 0x80 is 1.0 and anything above it saturates, so it is doubled for the PSP's own 0..1. */
static inline unsigned alpha_scale(unsigned a) {
    return a >= 0x80u ? 255u : a * 2u;
}

/* ------------------------------------------------------------------ graphics memory */
static void *tex_alloc(unsigned bytes) {
    void *p;

    unsigned alignedBytes = (bytes + 63u) & ~63u;
    if (alignedBytes > MAX_TEX_BYTES - sTexMem) {
        return NULL;
    }
    p = sVram + TEX_OFF + sTexMem;
    sTexMem += alignedBytes;
    return p;
}

/* VRAM is a bump allocator and cannot be reclaimed entry-by-entry.
   Main RAM entries, however, can be discarded under pressure, even if
   the nominal 10 MiB cache budget has not been reached.  This is needed:
   on PSP the interpreter, movie decoder and texture staging allocations
   all compete for the same small heap.  Only cached main-RAM entries can
   actually return memory to the allocator. */
static int tex_evict_one(void) {
    int i, oldest = -1;
    for (i = 0; i < sTexCount; ++i) {
        if (sTex[i].heap_alloc != NULL &&
            (oldest < 0 || sTex[i].last < sTex[oldest].last)) {
            oldest = i;
        }
    }
    if (oldest < 0) return 0;

    /* A key regression in the old PSP port: EVERY texture eviction,
       even an LRU entry last used 20 frames ago, finished and waited
       for the PSP GE. GsGpu_FrameEnd already synchronizes the entire
       previous frame before incrementing sFrame. Only a texture used
       in the CURRENT frame can still be referenced by an in-flight
       command. Do not unnecessarily stall the CPU for old ones.
       This safety rule applies in menus and fights equally. */
    if (Psp_TexNeedsGeFence(sTex[oldest].last, sFrame)) {
        unsigned long long begin = Psp_TimeUs();
        flush();
        sceGuFinish();
        sceGuSync(0, 0);
        sceGuStart(GU_DIRECT, sList);
        sAppliedValid = 0; /* fresh command list; force state setup */
        sTexEvictFenceUs += Psp_TimeUs() - begin;
        ++sTexEvictSameFrame;
    } else {
        ++sTexEvictAfterFrame;
    }
    free(sTex[oldest].heap_alloc);
    sTexRamMem -= sTex[oldest].bytes;
    memmove(&sTex[oldest], &sTex[oldest + 1],
            (size_t)(sTexCount - oldest - 1) * sizeof(Tex));
    --sTexCount;
    ++sTexSerial; /* descriptors shifted */
    ++sTexCacheEvicted;
    ++sTexPressureEvictions;
    sceGuTexFlush();
    return 1;
}

/* Called for ordinary cache-budget management. Descriptor exhaustion is
   extremely rare; if it occurs with VRAM-only textures, the only safe
   option is to forget an LRU descriptor while retaining its VRAM bytes. */
static void tex_evict(unsigned additionalRamBytes) {
    while (sTexCount >= MAX_TEX ||
           additionalRamBytes > sTexRamBudget - sTexRamMem) {
        int i, oldest = -1;
        if (tex_evict_one()) continue;
        if (sTexCount < MAX_TEX) break;
        for (i = 0; i < sTexCount; ++i) {
            if (oldest < 0 || sTex[i].last < sTex[oldest].last) oldest = i;
        }
        if (oldest < 0) break;
        memmove(&sTex[oldest], &sTex[oldest + 1],
                (size_t)(sTexCount - oldest - 1) * sizeof(Tex));
        --sTexCount;
        ++sTexSerial;
        ++sTexCacheEvicted;
    }
}

/* Recover from malloc failures in scratch buffers. Previously every
   failed attempt decoded the same texture again next draw, leading to
   thousands of GS draws dropped and ~2 seconds per 6-7 GS chains. */
/* Shared scratch buffers remain allocated until process exit; the
   input pixels are expanded/decoded anew per miss, so keeping these
   bytes never makes a stale texture visible. */
static void tex_temp_free(void *p) {
    if (p && p != (void *)sScratchRgba && p != (void *)sScratchIndices)
        free(p);
}
static void *tex_malloc_pressure(size_t bytes) {
    void *p = malloc(bytes);
    if (p != NULL) return p;
    ++sTexScratchFailures;
    while (tex_evict_one()) {
        p = malloc(bytes);
        if (p != NULL) {
            ++sTexScratchRecovered;
            break;
        }
    }
    return p;
}

/* PSP GE can fetch 16-bit / 32-bit swizzled textures from 64-byte aligned
   system RAM after flushing the D-cache. If malloc fails, free an older
   RAM cache entry and retry rather than repeatedly skipping GS draws. */
static void *tex_alloc_ram(unsigned bytes, void **heap_alloc) {
    void *base;
    uintptr_t aligned;
    *heap_alloc = NULL;
    if (bytes > sTexRamBudget) {
        ++sTexOversized;
        return NULL;
    }
    tex_evict(bytes);
    if (sTexCount >= MAX_TEX ||
        bytes > sTexRamBudget - sTexRamMem) return NULL;

    base = malloc((size_t)bytes + 63u);
    if (base == NULL) {
        while (tex_evict_one()) {
            base = malloc((size_t)bytes + 63u);
            if (base != NULL) {
                ++sTexRecoveredAllocations;
                break;
            }
        }
    }
    if (base == NULL) {
        ++sTexHeapFailed;
        return NULL;
    }
    aligned = ((uintptr_t)base + 63u) & ~(uintptr_t)63u;
    *heap_alloc = base;
    sTexRamMem += bytes;
    if (sTexRamMem > sTexRamPeak) sTexRamPeak = sTexRamMem;
    return (void *)aligned;
}

/* ------------------------------------------------------------------ targets */
/* A separate PSP framebuffer for the game's working passes. During the
   opening/menus the GS uses more than one FBP, which previously all mapped
   to FB0 and drew black rectangles over the actual title screen. */
static void *target_color(uint32_t fbp) {
    int screen = Psp_GsScenePairIsScreen(fbp, gGsMainFbp,
                                         gGsDisplayOtherFbp,
                                         gPspScenePairScreen);
    return sVram + (screen ? sMainDrawOffset : WORK_OFF);
}

static int target_get(uint32_t fbp, int create) {
    int i, slot = -1;

    for (i = 0; i < MAX_TARGETS; i++) {
        if (sTargets[i].used && sTargets[i].fbp == fbp) {
            sTargets[i].last = sFrame;
            sTargets[i].color = target_color(fbp);
            return i;
        }
    }
    if (!create) {
        return -1;
    }
    for (i = 0; i < MAX_TARGETS; i++) {
        if (!sTargets[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) { /* the one not drawn into for the longest time gives its place up */
        for (i = 0; i < MAX_TARGETS; i++) {
            if (sTargets[i].fbp != (uint32_t)gGsMainFbp && (slot < 0 || sTargets[i].last < sTargets[slot].last)) {
                slot = i;
            }
        }
    }
    if (slot < 0) {
        return -1;
    }
    sTargets[slot].used = 1;
    sTargets[slot].fbp = fbp;
    sTargets[slot].gen = 0;
    sTargets[slot].rendered = 0;  /* never sample uninitialized backing VRAM */
    sTargets[slot].last = sFrame;
    sTargets[slot].color = target_color(fbp);
    sTargets[slot].depth = sVram + DEPTH_OFF;
    if (++sTargetsRegistered <= 8) {
        Psp_Log("psp: GS framebuffer #%u fbp=%u mapped %s\n",
                sTargetsRegistered, fbp,
                sTargets[slot].color == sVram + WORK_OFF ? "WORK" : "SCREEN");
    }
    return slot;
}

/* ------------------------------------------------------------------ textures */
/* Legacy pixel-by-pixel hash retained for unsupported formats and
   intermittent runtime equivalence tests on actual BT3 palette data. */
static uint32_t clut_hash_slow(uint32_t cbp, uint32_t cpsm, uint32_t count) {
    uint32_t h = 2166136261u, i;
    for (i = 0; i < count; ++i)
        h = (h ^ Gs_VramRead(cbp, 1, cpsm, i & 15u, i >> 4)) * 16777619u;
    return h;
}

static uint32_t clut_hash(uint32_t cbp, uint32_t cpsm, uint32_t count) {
    uint32_t pixels[256], h;
    /* These four formats have the same block/column layouts in
       Gs_VramReadRect and Gs_VramRead. Leave unknown CPSM on the
       conservative legacy path; 16 and 256 entries are standard. */
    if (!sClutRectSafe || (count != 16u && count != 256u) ||
        (cpsm != 0x00u && cpsm != 0x01u &&
         cpsm != 0x02u && cpsm != 0x0Au))
        return clut_hash_slow(cbp, cpsm, count);

    Gs_VramReadRect(cbp, 1, cpsm, 16u, count / 16u, pixels);
    h = Psp_ClutHashPixels(pixels, count);
    ++sClutFastCalls;

    /* Even if a future GS swizzle implementation changes, mismatches
       fall back immediately and disable fast hashing for this run.
       Limit check overhead to one draw per 256 palette queries. */
    if ((sClutFastCalls & 255u) == 1u) {
        uint32_t reference = clut_hash_slow(cbp, cpsm, count);
        ++sClutSlowSamples;
        if (reference != h) {
            ++sClutHashMismatches;
            sClutRectSafe = 0;
            Psp_Log("psp: CLUT hash mismatch cbp=%u cpsm=%u count=%u; disabling fast palette reader\n",
                    cbp, cpsm, count);
            return reference;
        }
    }
    return h;
}

/* GS memory -> a PSP texture for the current TEX0. NULL when there is no room. */
/* The stored bits of a rectangle of GS memory, as gs_core.c has them. The Vita has block-at-a-time versions of
   these two in a Vita-only part of that file (one address computation per block instead of per pixel); the PSP
   builds them on Gs_VramRead, which every port has. A texture is decoded once, when the game uploads it. */
/* Gs_VramReadRect and Gs_VramReadIdx8 now use the fast, block-wise
   GS memory reader in gs_core.c for PSP builds. */
static void *texture_get(int ctx, unsigned lodCap, int white_sprite,
                         int *tw_out, int *th_out, int *fmt_out) {
    uint64_t t0 = gGs.tex0[ctx], texa = gGs.texa;
    /* Effective palette is computed for the texture cache without
       changing TEX0. This helper is exercised by test_gs_experiments.c. */
    if (gPspGsClutReuse) {
        extern volatile uint64_t gGsClutT0;
        t0 = Psp_GsEffectiveClut(t0, gGsClutT0);
    }
    uint32_t tbp = (uint32_t)(t0 & 0x3FFF), tbw = (uint32_t)((t0 >> 14) & 0x3F), psm = (uint32_t)((t0 >> 20) & 0x3F);
    uint32_t tw = 1u << ((t0 >> 26) & 15), th = 1u << ((t0 >> 30) & 15);
    uint32_t cbp = (uint32_t)((t0 >> 37) & 0x3FFF), cpsm = (uint32_t)((t0 >> 51) & 15);
    uint32_t bits = (uint32_t)Gs_PsmBits(psm), pages, i, alphaNeed = 0;
    uint32_t readW, readH;
    uint32_t gen = 0;
    Tex *t;
    uint32_t *px;
    int fmt;
    /* Sampling the lookup one time in 32 avoids adding a clock syscall
       to every cache hit during the game's thousands of GS draws. */
    int sampleKey = ((++sProfKeyCalls & 31u) == 0);
    unsigned long long keyStartUs = sampleKey ? Psp_TimeUs() : 0;
    unsigned reuseSlot;
    unsigned long long decodeStartUs;

    if (tw > 512) { tw = 512; }
    if (th > 512) { th = 512; }
    /* Always hash and decode the ORIGINAL GS source rectangle. The cap
       describes only the independent GPU copy and must not truncate GS
       memory lookups. It is enabled for polygons only, not HUD sprites. */
    readW = tw; readH = th;
    tw = Psp_LodDimension(tw, lodCap);
    th = Psp_LodDimension(th, lodCap);
    reuseSlot = Psp_TexReuseSlot(t0, texa, lodCap, white_sprite);
    if (Psp_TexReuseMatches(&sTexReuse[reuseSlot], t0, texa,
                            gGsPspTextureEpoch, sTexSerial,
                            lodCap, white_sprite, (unsigned)sTexCount)) {
        /* Source GS VRAM/CLUT and texture table unchanged since last key. */
        t = &sTex[sTexReuse[reuseSlot].index];
        ++sTexReuseHits;
        ++sTexCacheHits;
        if (white_sprite) ++sDiagWhiteTextureHits;
        if (tw != readW || th != readH) ++sLodCacheHits;
        t->last = sFrame;
        *tw_out = (int)t->w;
        *th_out = (int)t->h;
        *fmt_out = t->fmt;
        sTexSwizzled = t->swizzled;
        if (sampleKey) {
            sProfKeySampleUs += Psp_TimeUs() - keyStartUs;
            ++sProfKeySamples;
        }
        return t->ptr;
    }
    ++sTexReuseFullKeys;
    pages = ((tbw ? tbw : 1) * 64 * readH * (bits == 24 ? 32 : bits) / 8 + 8191) / 8192;
    for (i = 0; i <= pages; i++) {
        gen = (gen ^ Gs_PageHash(tbp / 32 + i)) * 16777619u + i;
    }
    if (bits <= 8) {
        gen = (gen ^ clut_hash(cbp, cpsm, bits == 8 ? 256 : 16)) * 16777619u;
    }
    for (i = 0; i < sTexCount; i++) {
        if (sTex[i].tex0 == t0 && sTex[i].texa == texa && sTex[i].gen == gen &&
            sTex[i].w == tw && sTex[i].h == th &&
            sTex[i].white_sprite == white_sprite) {
            if (white_sprite) ++sDiagWhiteTextureHits;
            if (tw != readW || th != readH) ++sLodCacheHits;
            ++sTexCacheHits;
            sTex[i].last = sFrame;
            Psp_TexReuseRemember(&sTexReuse[reuseSlot], t0, texa,
                                 gGsPspTextureEpoch, sTexSerial,
                                 lodCap, white_sprite, (unsigned)i);
            *tw_out = (int)sTex[i].w;
            *th_out = (int)sTex[i].h;
            *fmt_out = sTex[i].fmt;
            sTexSwizzled = sTex[i].swizzled;
            if (sampleKey) {
                sProfKeySampleUs += Psp_TimeUs() - keyStartUs;
                ++sProfKeySamples;
            }
            return sTex[i].ptr;
        }
    }
    if (sampleKey) {
        sProfKeySampleUs += Psp_TimeUs() - keyStartUs;
        ++sProfKeySamples;
    }
    ++sTexCacheMisses;
    decodeStartUs = Psp_TimeUs();
    if (sScratchRgba && Psp_TexUseScratch(
            readW * readH * 4u, PSP_TEX_SCRATCH_RGBA_BYTES)) {
        px = sScratchRgba;
        ++sScratchRgbaUses;
    } else {
        px = tex_malloc_pressure((size_t)readW * readH * 4u);
    }
    if (px == NULL) {
        return NULL;
    }
    if (bits > 8) {
        Gs_VramReadRect(tbp, tbw, psm, readW, readH, px);
        if (readW != tw || readH != th)
            Psp_LodSubsample32(px, readW, readH, tw, th);
        for (i = 0; i < tw * th; i++) {
            uint32_t c = Gs_Expand(px[i], psm), a = (c >> 24) & 0xFF;
            px[i] = c;
            if (a != 0 && a != 0x80 && a != 0xFF) { alphaNeed = 1; }
        }
    } else {
        /* PS2 CLUTs contain only 16 or 256 entries. Previously we called
           Gs_VramRead() and Gs_Expand() for EVERY texture pixel, so a
           512x512 indexed sprite repeated the same palette lookups 262144
           times. Read/expand the palette once, then use direct array
           lookups. Keep the exact GS CLUT swizzle, alpha, and TEXA rules. */
        uint32_t palette[256];
        uint32_t count = bits == 8 ? 256u : 16u;
        uint8_t *idx;
        if (sScratchIndices && Psp_TexUseScratch(
                readW * readH, PSP_TEX_SCRATCH_INDEX_BYTES)) {
            idx = sScratchIndices;
            ++sScratchIndicesUses;
        } else {
            idx = tex_malloc_pressure((size_t)readW * readH);
        }
        if (idx == NULL) {
            tex_temp_free(px);
            return NULL;
        }
        for (i = 0; i < count; i++) {
            uint32_t e = bits == 8
                       ? ((i & 0xE7u) | ((i & 8u) << 1) | ((i & 0x10u) >> 1))
                       : i;
            uint32_t c = Gs_VramRead(cbp, 1, cpsm,
                        bits == 8 ? (e & 15u) : (e & 7u),
                        bits == 8 ? (e >> 4) : (e >> 3));
            palette[i] = Gs_Expand(c, cpsm);
        }
        /* Standalone 8/4-bit and upper-byte/nibble GS formats have
           different layouts. Do not consume undefined indices if the
           GS decoder doesn't support the requested PSM. */
        if (!Gs_VramReadIdx8(tbp, tbw, psm, readW, readH, idx, readW)) {
            if (Psp_GsIsHighIndexed(psm)) ++sDiagHiTexDecodeFailed;
            tex_temp_free(idx);
            tex_temp_free(px);
            return NULL;
        }
        if (readW != tw || readH != th)
            Psp_LodSubsample8(idx, readW, readH, tw, th);
        for (i = 0; i < tw * th; i++) {
            uint32_t c = palette[idx[i] & (count - 1u)];
            uint32_t a = (c >> 24) & 0xFF;
            px[i] = c;
            if (a != 0 && a != 0x80 && a != 0xFF) { alphaNeed = 1; }
        }
        tex_temp_free(idx);
    }
    /* Exact PR #22 fix (fe93677 base): the PS2 GS MODULATE uses
       textureRGB*vertexRGB/128; PSP GU divides by 255. Bake RGB only
       in a SEPARATE texture variant for fully-white GS font sprites.
       Keep the normal decoded cache, palettes, TEXA and alpha untouched. */
    if (white_sprite) {
        for (i = 0; i < tw * th; ++i)
            px[i] = Psp_GsWhitePixel(px[i]);
        ++sDiagWhiteTextureBuilds;
    }
    /* Gs_Expand() already applies TEXA for 16/24-bit pixels and CLUT entries.
       Applying GS TEXA again here changed the alpha of valid palette colours. */
    fmt = alphaNeed ? GU_PSM_8888 : GU_PSM_5551;
    {
        unsigned bytes = (unsigned)(tw * th * (fmt == GU_PSM_8888 ? 4 : 2));
        unsigned alignedBytes = (bytes + 63u) & ~63u;
        int doSwizzle = tw * (fmt == GU_PSM_8888 ? 4u : 2u) >= 16u && th >= 8u;
        void *ptr = NULL, *heap_alloc = NULL;
        /* Leave existing VRAM-resident textures intact: they work well
           for the opening screens. Store overflow and oversized images
           in PSP system RAM, instead of continuously resetting the cache. */
        if (sTexCount >= MAX_TEX) tex_evict(0);
        if (bytes <= MAX_TEX_BYTES / 2 &&
            alignedBytes <= MAX_TEX_BYTES - sTexMem) {
            ptr = tex_alloc(bytes);
        }
        if (ptr == NULL) ptr = tex_alloc_ram(bytes, &heap_alloc);
        if (ptr == NULL) {
            tex_temp_free(px);
            return NULL;
        }
        /* Reuse the already-decoded 32-bit working pixels as the output
           staging buffer. The old code allocated another 512 KiB - 1 MiB
           temporary buffer for every miss; under memory pressure that
           extra allocation failed after VRAM had already been reserved.
           Converting RGBA32 -> 5551 in-place is safe in increasing index
           order: each destination consumes 2 bytes while each future
           input pixel starts 4 bytes later. */
        if (fmt == GU_PSM_8888) {
            for (i = 0; i < tw * th; ++i) {
                uint32_t c = px[i], a = alpha_scale((c >> 24) & 0xFF);
                px[i] = (c & 0x00FFFFFFu) | (a << 24);
            }
            if (doSwizzle) swizzle((unsigned char *)ptr,
                                   (const unsigned char *)px, tw, th, 4);
            else memcpy(ptr, px, bytes);
        } else {
            uint16_t *d = (uint16_t *)px;
            for (i = 0; i < tw * th; ++i) {
                uint32_t c = px[i];
                unsigned r = (c >> 3) & 0x1F, g = (c >> 11) & 0x1F;
                unsigned b = (c >> 19) & 0x1F;
                unsigned a = ((c >> 24) & 0xFF) ? 1 : 0;
                d[i] = (uint16_t)(r | (g << 5) | (b << 10) | (a << 15));
            }
            if (doSwizzle) swizzle((unsigned char *)ptr,
                                   (const unsigned char *)px, tw, th, 2);
            else memcpy(ptr, px, bytes);
        }
        /* The VRAM mapping is uncached. For PSP main RAM write back only
           this texture; flushing the full data cache on every miss was
           particularly expensive during texture-heavy game scenes. */
        if (heap_alloc != NULL) sceKernelDcacheWritebackRange(ptr, bytes);
        sceGuTexFlush();
        t = &sTex[sTexCount++];
        ++sTexSerial;
        Psp_TexReuseRemember(&sTexReuse[reuseSlot], t0, texa,
                             gGsPspTextureEpoch, sTexSerial,
                             lodCap, white_sprite, (unsigned)(sTexCount - 1));
        t->tex0 = t0;
        t->texa = texa;
        t->gen = gen;
        t->ptr = ptr;
        t->heap_alloc = heap_alloc;
        t->w = tw;
        t->h = th;
        t->fmt = fmt;
        t->swizzled = doSwizzle;
        t->white_sprite = white_sprite;
        sTexSwizzled = doSwizzle;
        t->bytes = bytes;
        t->last = sFrame;
        sNewTex++;
        if (tw != readW || th != readH) {
            ++sLodReduced;
            sLodPixelsSaved += (unsigned long long)readW * readH - (unsigned long long)tw * th;
        }
        *tw_out = (int)tw;
        *th_out = (int)th;
        *fmt_out = fmt;
        tex_temp_free(px);
        sProfDecodeUs += Psp_TimeUs() - decodeStartUs;
        ++sProfDecodeCount;
        return ptr;
    }
}

/* ------------------------------------------------------------------ state */
/* Emit GE state commands only for groups changed since the previous
   batch. This is render-state batching rather than geometry reordering:
   every primitive remains in exact PS2 submission order. */
static void state_apply(void) {
    int first = !sAppliedValid;
    const __typeof__(sSt) *p = &sApplied;
    ++sGuStateCalls;
    if (first || memcmp(&sSt, p, sizeof(sSt)) != 0) ++sGuStateChanges;

    if (sSt.target != NULL && (first || sSt.target != p->target)) {
        unsigned offset = (unsigned)((uintptr_t)sSt.target - (uintptr_t)sVram);
        sceGuDrawBufferList(GU_PSM_5551, (void *)(uintptr_t)offset, BUF_W);
        ++sGuTargetChanges;
    }

    if (sSt.blend_on) {
        if (first || !p->blend_on ||
            sSt.blendop != p->blendop || sSt.src != p->src ||
            sSt.dst != p->dst || sSt.sfix != p->sfix ||
            sSt.dfix != p->dfix) {
            /* GU_FIX takes packed RGB; expand scalar across channels. */
            sceGuBlendFunc(sSt.blendop, sSt.src, sSt.dst,
                           (unsigned)sSt.sfix * 0x00010101u,
                           (unsigned)sSt.dfix * 0x00010101u);
        }
        if (first || !p->blend_on) sceGuEnable(GU_BLEND);
    } else if (first || p->blend_on) {
        sceGuDisable(GU_BLEND);
    }

    /* The PS2 health gauge writes alpha-only silhouettes to FRAME_2 and
       reads them through TEST_1.DATE. On PSP RGB5551, the framebuffer
       alpha bit IS a one-bit stencil surface. Translate this masked
       2D HUD subgroup without changing model textures or game HP. */
    if (first || sSt.pixelMask != p->pixelMask)
        sceGuPixelMask(sSt.pixelMask);
    if (first || sSt.stencilMode != p->stencilMode) {
        if (sSt.stencilMode == PSP_HUD_MASK_NONE) {
            sceGuDisable(GU_STENCIL_TEST);
        } else {
            int value = sSt.stencilMode == PSP_HUD_MASK_SET ||
                        sSt.stencilMode == PSP_HUD_MASK_DATE_ONE ? 0xFF : 0;
            if (sSt.stencilMode == PSP_HUD_MASK_CLEAR ||
                sSt.stencilMode == PSP_HUD_MASK_SET) {
                sceGuStencilFunc(GU_ALWAYS, value, 0xFF);
                sceGuStencilOp(GU_KEEP, GU_KEEP, GU_REPLACE);
            } else {
                sceGuStencilFunc(GU_EQUAL, value, 0xFF);
                sceGuStencilOp(GU_KEEP, GU_KEEP, GU_KEEP);
            }
            sceGuEnable(GU_STENCIL_TEST);
        }
    }

    if (sSt.atest) {
        if (first || !p->atest || sSt.afunc != p->afunc ||
            sSt.aref != p->aref || sSt.amask != p->amask) {
            sceGuAlphaFunc(sSt.afunc, sSt.aref, sSt.amask);
        }
        if (first || !p->atest) sceGuEnable(GU_ALPHA_TEST);
    } else if (first || p->atest) {
        sceGuDisable(GU_ALPHA_TEST);
    }

    /* Depth is OFF by default (the fe93677 baseline). gs_depth=1 is the
       stack's original experiment (PR #5/#9): preserve TEST.ZTE/ZTST and
       ZBUF.ZMSK per batch on every target. gs_depth=2 restricts that to
       the screen target: the PSP has ONE depth buffer shared with the
       WORK target, so work passes neither test nor write and cannot
       pollute the scene's depth. Without any of it, submission order
       decides visibility -- a fighter's far side can paint over its near
       side (see docs/psp/DIAGNOSTICO_FPS_TEXTURAS_2026-10-26.md).
       Special FB16/depth-as-texture passes are not yet fully mapped. */
    {
        int depthHere = gPspGsDepth == 1 ||
                        (gPspGsDepth == 2 && sSt.target != sVram + WORK_OFF);
        int depthPrev = gPspGsDepth == 1 ||
                        (gPspGsDepth == 2 && p->target != sVram + WORK_OFF);
        if (!depthHere) {
            if (first || depthPrev) {
                sceGuDisable(GU_DEPTH_TEST);
                sceGuDepthMask(GU_TRUE); /* GU_TRUE: Z writes off (pspsdk) */
            }
        } else {
            int currentDepthOn = sSt.ztest || sSt.zwrite;
            int previousDepthOn = depthPrev && (p->ztest || p->zwrite);
            if (currentDepthOn) {
                if (first || !previousDepthOn ||
                    sSt.zfunc != p->zfunc || sSt.ztest != p->ztest)
                    sceGuDepthFunc(sSt.ztest ? sSt.zfunc : GU_ALWAYS);
                if (first || !previousDepthOn) sceGuEnable(GU_DEPTH_TEST);
            } else if (first || previousDepthOn) {
                sceGuDisable(GU_DEPTH_TEST);
            }
            if (first || !depthPrev || sSt.zwrite != p->zwrite)
                sceGuDepthMask(sSt.zwrite ? GU_FALSE : GU_TRUE);
        }
    }

    if (first || sSt.scx != p->scx || sSt.scy != p->scy ||
        sSt.scw != p->scw || sSt.sch != p->sch) {
        int x0 = (int)((float)sSt.scx * (float)SCR_W / 512.0f);
        int y0 = (int)((float)sSt.scy * (float)SCR_H / 448.0f);
        int x1 = (int)((float)(sSt.scx + sSt.scw) * (float)SCR_W / 512.0f + 0.999f);
        int y1 = (int)((float)(sSt.scy + sSt.sch) * (float)SCR_H / 448.0f + 0.999f);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > SCR_W) x1 = SCR_W;
        if (y1 > SCR_H) y1 = SCR_H;
        if (x1 > x0 && y1 > y0) {
            sceGuScissor(x0, y0, x1, y1);
            sceGuEnable(GU_SCISSOR_TEST);
        } else {
            sceGuDisable(GU_SCISSOR_TEST);
        }
    }

    if (sSt.tex != NULL) {
        if (first || p->tex == NULL || sSt.fmt != p->fmt ||
            sSt.tmode != p->tmode) {
            sceGuTexMode(sSt.fmt, 0, 0, sSt.tmode);
        }
        /* A work framebuffer can be changed between two reads of the
           SAME address; therefore always invalidate on a FB texture bind,
           even when the other texture registers were unchanged. */
        if (sSt.tex == sVram + WORK_OFF ||
            sSt.tex == sVram + FB0_OFF ||
            sSt.tex == sVram + FB1_OFF) {
            sceGuTexFlush();
        }
        if (first || p->tex != sSt.tex || p->tw != sSt.tw ||
            p->th != sSt.th || p->fmt != sSt.fmt ||
            p->tmode != sSt.tmode) {
            sceGuTexImage(0, sSt.tw, sSt.th, sSt.tw, sSt.tex);
        }
        if (first || p->tex == NULL || sSt.tfx != p->tfx ||
            sSt.tcc != p->tcc) {
            sceGuTexFunc(sSt.tfx, sSt.tcc);
        }
        if (first || p->tex == NULL || sSt.wrapu != p->wrapu ||
            sSt.wrapv != p->wrapv) {
            sceGuTexWrap(sSt.wrapu, sSt.wrapv);
        }
        if (first || p->tex == NULL || sSt.filt != p->filt) {
            sceGuTexFilter(sSt.filt, sSt.filt);
        }
        if (first || p->tex == NULL) sceGuEnable(GU_TEXTURE_2D);
    } else if (first || p->tex != NULL) {
        sceGuDisable(GU_TEXTURE_2D);
    }

    memcpy(&sApplied, &sSt, sizeof(sSt));
    sAppliedValid = 1;
}

/* ---- movie presentation: the PS2 decoder writes the movie to GS VRAM
   via host-to-local GIF transfers. Those transfers update emulated GS
   memory but never become PSP GU draw commands. Present the MPEG decoder's
   RGBA pictures directly, after other drawing and before the buffer swap.
   This only changes the PSP backend; the game/player and audio are intact. */
void Psp_GuMovieFrame(const uint8_t *rgba) {
    sMovieDirect5551 = NULL;
    sMovieRgba = rgba;
    ++sMovieSubmitted;
    if (sMovieSubmitted == 1 && rgba != NULL) {
        const unsigned char *c = rgba + (((MOVIE_SRC_H / 2) * MOVIE_SRC_W + MOVIE_SRC_W / 2) * 4);
        Psp_Log("psp: movie: first MPEG frame decoded, center RGB=%u,%u,%u\n",
                (unsigned)c[0], (unsigned)c[1], (unsigned)c[2]);
    }
}

void Psp_GuMovieFrame16(const uint16_t *rgb5551) {
    sMovieRgba = NULL;
    sMovieDirect5551 = rgb5551;
    ++sMovieSubmitted;
    if (sMovieSubmitted == 1 && rgb5551 != NULL) {
        uint16_t c = rgb5551[(MOVIE_TEX_H / 2) * MOVIE_TEX_W + MOVIE_DISPLAY_W / 2];
        Psp_Log("psp: movie: first direct MPEG RGB5551 frame, center pixel=%04x\n", (unsigned)c);
    }
}

void Psp_GuMovieClose(void) {
    if (sMovieSubmitted != 0) {
        Psp_Log("psp: movie: %u decoded frames, %u GU frames queued\n",
                sMovieSubmitted, sMovieDisplayed);
    }
    if (sMovieDisplayed != 0) {
        Psp_Log("psp: movie conversion: %u frames avg %u ms, max %u ms\n",
                sMovieDisplayed,
                (unsigned)(sMovieConvertTotalUs / sMovieDisplayed / 1000u),
                sMovieConvertMaxUs / 1000u);
    }
    sMovieRgba = NULL;
    sMovieDirect5551 = NULL;
    sMovieSubmitted = 0;
    sMovieDisplayed = 0;
    sMovieConvertTotalUs = 0;
    sMovieConvertMaxUs = 0;
}

static void movie_draw_frame(void) {
    unsigned y, x;
    const uint8_t *rgba = sMovieRgba;
    const uint16_t *texPixels = sMovieDirect5551;
    unsigned long long convertStart;
    if (texPixels == NULL && rgba == NULL) return;
    convertStart = Psp_TimeUs();

    if (texPixels == NULL) {
    /* Nearest-neighbour resize from 512x448 RGBA32 to 480x256 RGBA5551.
       Integer math keeps this portable on the Allegrex without heavy
       per-pixel float operations; the GU scales height to 272 pixels. */
    for (y = 0; y < MOVIE_TEX_H; ++y) {
        unsigned sy = y * MOVIE_SRC_H / MOVIE_TEX_H;
        const uint8_t *srcRow = rgba + sy * MOVIE_SRC_W * 4u;
        uint16_t *dstRow = sMoviePixels + y * MOVIE_TEX_W;
        for (x = 0; x < MOVIE_DISPLAY_W; ++x) {
            const uint8_t *c = srcRow + (x * MOVIE_SRC_W / MOVIE_DISPLAY_W) * 4u;
            dstRow[x] = (uint16_t)(0x8000u | ((unsigned)c[0] >> 3)
                      | (((unsigned)c[1] >> 3) << 5)
                      | (((unsigned)c[2] >> 3) << 10));
        }
    }
    texPixels = sMoviePixels;
    {
        unsigned elapsedUs = (unsigned)(Psp_TimeUs() - convertStart);
        sMovieConvertTotalUs += elapsedUs;
        if (elapsedUs > sMovieConvertMaxUs) sMovieConvertMaxUs = elapsedUs;
    }
    }
    /* In either mode the CPU writes the texture just before the GE reads
       it. Flush only these 256 KiB rather than the entire data cache. */
    sceKernelDcacheWritebackRange((void *)texPixels, sizeof(sMoviePixels));

    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuTexMode(GU_PSM_5551, 0, 0, 0);
    sceGuTexImage(0, MOVIE_TEX_W, MOVIE_TEX_H, MOVIE_TEX_W, texPixels);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexFlush(); /* CPU updates the same texture memory each frame */
    sceGuEnable(GU_TEXTURE_2D);

    memset(sMovieVertices, 0, sizeof(sMovieVertices));
    sMovieVertices[0].c = 0xFFFFFFFFu;
    sMovieVertices[1].c = 0xFFFFFFFFu;
    sMovieVertices[1].u = (float)MOVIE_DISPLAY_W;
    sMovieVertices[1].v = (float)MOVIE_TEX_H;
    sMovieVertices[1].x = (float)SCR_W;
    sMovieVertices[1].y = (float)SCR_H;
    sceKernelDcacheWritebackRange(sMovieVertices, sizeof(sMovieVertices));
    sceGuDrawArray(GU_SPRITES,
                  GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                  2, NULL, sMovieVertices);
    if (++sMovieDisplayed % 60u == 0) {
        Psp_Log("psp: movie: %u GU frames, legacy RGBA conversion avg %u ms, max %u ms\n",
                sMovieDisplayed,
                (unsigned)(sMovieConvertTotalUs / sMovieDisplayed / 1000u),
                sMovieConvertMaxUs / 1000u);
    }
    if (sMovieDisplayed == 1) {
        Psp_Log("psp: movie: first frame sent to PSP GU (480x272)\\n");
    }
}

/* ------------------------------------------------------------------ drawing */
static void flush(void) {
    if (sVbCount == 0) {
        return;
    }
    state_apply();
    sceKernelDcacheWritebackRange(sVb, (size_t)sVbCount * sizeof(Vtx));
    sceGuDrawArray(sVbMode, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, sVbCount, NULL, sVb);
    sVbCount = 0;
    sBatches++;
}

void GsGpu_Draw(int type, int ctx, const GsVertex *v) {
    uint64_t prim = gGs.prim, t0 = gGs.tex0[ctx], test = gGs.test[ctx], sc = gGs.scissor[ctx];
    uint64_t cl = gGs.clamp[ctx], frame = gGs.frame[ctx];
    int fbp = (int)(frame & 0x1FF), zbp = (int)(gGs.zbuf[ctx] & 0x1FF);
    int tme = (int)((prim >> 4) & 1), n = type == 6 ? 2 : type == 3 ? 3 : type == 1 ? 2 : 1;
    int mode = type == 6 ? GU_SPRITES : type == 3 ? GU_TRIANGLES : type == 1 ? GU_LINES : GU_POINTS;
    int i;
    float sx, sy;
    Vtx *d;
    int tw = 0, th = 0, fmt = 0;
    void *tex = NULL;
    float us = 1.0f, vs = 1.0f;
    float lodU = 1.0f, lodV = 1.0f;
    int fst = (int)((prim >> 8) & 1);
    int texIsTarget = 0, targetIndex;
    int topHudSprite = 0, hudMaskMode = PSP_HUD_MASK_NONE;
    /* This is the exact white flat-sprite subset proven by PR #22.
       type==6 is tested before dereferencing the second sprite vertex. */
    int whiteSprite = gPspWhiteModulate && type == 6 &&
        Psp_GsIsWhiteModSprite((unsigned)type, tme,
                              (unsigned)((t0 >> 35) & 3u),
                              ((uint32_t)v[1].r | ((uint32_t)v[1].g << 8) |
                               ((uint32_t)v[1].b << 16) | ((uint32_t)v[1].a << 24)));
    int profSample = ((++sProfDrawCalls & 15u) == 0);
    unsigned long long profStart = profSample ? Psp_TimeUs() : 0;

    if (!sReady) {
        if (profSample) {
            sProfDrawSampleUs += Psp_TimeUs() - profStart;
            ++sProfDrawSamples;
        }
        return;
    }
    sDraws++;
    /* GS frame PSMCT16 colour pixels have the same 5551 channel layout
       as PSP GU_PSM_5551. Direct output is now supported only for PSM
       0x02 and only when colour does NOT alias the depth page.
       The PS2 16S swizzle, framebuffer readbacks and depth-as-texture
       are still unsupported and must use the previous skip path. */
    {
        uint32_t framePsm = (uint32_t)((frame >> 24) & 0x3Fu);
        int is16 = Gs_PsmBits(framePsm) == 16;
        int native16 = is16 &&
          Psp_GsCanRenderColor16(framePsm, (uint32_t)fbp,
                                 (uint32_t)zbp, gPspGsFb16);
        if (native16) ++sDiagFb16Native;
        if ((!native16 && is16) || fbp == zbp) {
            if (is16 && !native16) ++sDiagSkip16;
            else ++sDiagSkipDepth;
        sSkipped++;
        if (profSample) {
            sProfDrawSampleUs += Psp_TimeUs() - profStart;
            ++sProfDrawSamples;
        }
            return;
        }
    }
    if (tme) {
        uint32_t tpsm = (uint32_t)((t0 >> 20) & 0x3F), tbp = (uint32_t)(t0 & 0x3FFF);
        /* Legacy framebuffer/depth-as-texture passes stay skipped.
           High-indexed colour texture reads are opt-in until PPSSPP
           A/B tests establish which scenes use them successfully. */
        if (Psp_GsIsHighIndexed(tpsm)) {
            if (gPspGsHiTex) ++sDiagHiTexAllowed;
            else ++sDiagHiTexBlocked;
        }
        if (tbp / 32 == (uint32_t)zbp ||
            (Psp_GsIsHighIndexed(tpsm) && !gPspGsHiTex) ||
            (tpsm & 0x30) == 0x30) {
            ++sDiagSkipPsm;
            sSkipped++;
            if (profSample) {
                sProfDrawSampleUs += Psp_TimeUs() - profStart;
                ++sProfDrawSamples;
            }
            return;
        }
        int src = target_get(tbp / 32, 0);
        int targetReady = 0;
        if (src >= 0) {
            const Target *old = &sTargets[src];
            /* A GS memory page can hold a framebuffer at one time and an
               ordinary texture after a host-to-local upload. Do not bind
               stale PSP framebuffer VRAM when the GS upload generation has
               changed. This matches the reference GPU's alias check. */
            if ((tbp & 31u) == 0 && old->rendered &&
                old->gen == gGsPageGen[old->fbp & 511] &&
                old->fbp != (uint32_t)fbp &&
                /* Two PS2 display FBPs can share one PSP draw surface,
                   which must never be read as texture while being written. */
                old->color != target_color((uint32_t)fbp) &&
                (old->color != sVram + WORK_OFF || sWorkOwnerFbp == old->fbp)) {
                targetReady = 1;
                ++sFbAliasHit;
            } else {
                ++sFbAliasFallback;
                if (old->gen != gGsPageGen[old->fbp & 511]) {
                    ++sFbAliasReupload;
                } else if (!old->rendered) {
                    ++sFbAliasUnrendered;
                } else if (old->fbp == (uint32_t)fbp) {
                    ++sFbAliasSelf;
                }
            }
        }
        if (!targetReady) {
            tex = texture_get(ctx, type == 3 ? (unsigned)gPspTexLimit : 0u,
                              whiteSprite, &tw, &th, &fmt);
            if (tex == NULL) {
                ++sDiagSkipTexMem;
                sSkipped++;
                if (profSample) {
                    sProfDrawSampleUs += Psp_TimeUs() - profStart;
                    ++sProfDrawSamples;
                }
                return;
            }
            /* GS FST UVs are in source texels: scale only those.
               ST uses normalised coordinates and already multiplies by the
               smaller tw/th below. Do not touch framebuffer-as-texture UV. */
            if (type == 3 && gPspTexLimit) {
                uint32_t srcW = 1u << ((t0 >> 26) & 15u);
                uint32_t srcH = 1u << ((t0 >> 30) & 15u);
                if (srcW > 512u) srcW = 512u;
                if (srcH > 512u) srcH = 512u;
                lodU = (float)tw / (float)srcW;
                lodV = (float)th / (float)srcH;
            }
        } else {
            /* Framebuffer alias pixels are not decoded cache-owned texture
               copies; never apply the white-font correction to an alias. */
            whiteSprite = 0;
            /* A valid framebuffer viewed as a texture is linear. Its physical
               pitch is 512 and PSP textures require power-of-two dimensions. */
            texIsTarget = 1;
            tex = sTargets[src].color;
            tw = 512;
            th = 256;
            fmt = GU_PSM_5551;
            us = (float)(1u << ((t0 >> 26) & 15)) / (float)SCR_W;
            vs = (float)(1u << ((t0 >> 30) & 15)) / (float)SCR_H;
            if (us > 1.0f) { us = 1.0f; }
            if (vs > 1.0f) { vs = 1.0f; }
        }
    }
    targetIndex = target_get((uint32_t)fbp, 1);
    if (targetIndex < 0) {
        ++sDiagSkipNoTarget;
        sSkipped++;
        if (profSample) {
            sProfDrawSampleUs += Psp_TimeUs() - profStart;
            ++sProfDrawSamples;
        }
        return;
    }
    /* Points only provide one vertex; do not read v[1] unless
       this is actually a 2-vertex GS sprite. */
    if (type == 6) {
        topHudSprite = Psp_GsIsBattleHudSprite(type,
            sTargets[targetIndex].color != sVram + WORK_OFF,
            v[0].x, v[0].y, v[1].x, v[1].y);
    }
    if (gPspHudMaskFix && type == 6 &&
        sTargets[targetIndex].color != sVram + WORK_OFF &&
        Psp_GsIsFullHudAlphaResetStrip(
            (unsigned)type, (unsigned)prim, (unsigned)gGs.alpha[ctx],
            (unsigned)test, v[0].x, v[0].y, v[1].x, v[1].y,
            v[1].r, v[1].g, v[1].b, v[1].a)) {
        /* HudGfx_ClearAlpha() is invoked TWICE per frame, once before
           side 0 and once before mirrored side 1. The PS2 first resets
           framebuffer alpha with 16 vertical strips, leaving RGB intact.
           PR #34 recognized only TRIANGLE-based small mask operations
           and carried side 0's stencil into side 1. */
        hudMaskMode = PSP_HUD_MASK_CLEAR;
        ++sHudMaskFullClearStrips;
        if (v[0].x >= 479.0f && v[0].x <= 482.0f)
            ++sHudMaskFullClearSweeps;
    }
    if (gPspHudMaskFix && type == 3 &&
        sTargets[targetIndex].color != sVram + WORK_OFF) {
        /* HUD texture rectangles use four-vertex PS2 TRIANGLE STRIPS
           (PRIM=0x154), dispatched as two GsGpu_Draw(type=3) calls.
           Previous fix wrongly selected only PRIM=SPRITE/type=6. */
        float hx[3] = {v[0].x, v[1].x, v[2].x};
        float hy[3] = {v[0].y, v[1].y, v[2].y};
        if (Psp_GsIsTopHudTriangle(hx, hy)) {
            unsigned zrgb = 1u;
            int k;
            for (k = 0; k < 3; k++) {
                if (v[k].r || v[k].g || v[k].b || v[k].a) zrgb = 0;
            }
            hudMaskMode = Psp_GsHudMaskClassify(
                (unsigned)(frame >> 32), (unsigned)gGs.fba[ctx],
                (unsigned)test, (unsigned)prim, (unsigned)gGs.alpha[ctx],
                zrgb);
        }
    }
    if (topHudSprite) {
        ++sHudTopSpriteCount;
        ++sHudTopTfxModes[(unsigned)((t0 >> 35) & 3u)];
        ++sHudTopClampModes[(unsigned)(cl & 3u)];
    }
    if (sTargets[targetIndex].color == sVram + WORK_OFF) {
        ++sDiagDrawWork;
        ++sFrameWorkDraws;
        ++sPairedWorkDraws;
    } else {
        ++sDiagDrawMain;
        ++sFrameScreenDraws;
        ++sPairedScreenDraws;
        if (gPspScenePairScreen && gGsDisplayOtherFbp >= 0 &&
            (uint32_t)fbp == (uint32_t)gGsDisplayOtherFbp &&
            gGsMainFbp != gGsDisplayOtherFbp) {
            ++sPairedDisplayRouted;
            ++sPairedThisFrame;
        }
    }
    /* What depth state did the GS ask for on this primitive, and do its
       Z values vary? Cheap counters only; reported (and reset) by
       Psp_GuStats every two seconds. */
    if (type == 3) {
        ++sDiagZTri;
        if (sTargets[targetIndex].color == sVram + WORK_OFF) ++sDiagZTriWork;
        if ((test >> 16) & 1) {
            ++sDiagZTriTest;
            ++sDiagZFunc[(test >> 17) & 3];
        }
        if (!((gGs.zbuf[ctx] >> 32) & 1)) ++sDiagZTriWrite;
        if (v[0].z == v[1].z && v[1].z == v[2].z) ++sDiagZTriFlat;
        for (i = 0; i < 3; ++i) {
            if (v[i].z < sDiagZMin) sDiagZMin = v[i].z;
            if (v[i].z > sDiagZMax) sDiagZMax = v[i].z;
        }
    } else if (type == 6) {
        ++sDiagZSprite;
        if ((test >> 16) & 1) ++sDiagZSpriteTest;
    }
    if (test & 1) {
        unsigned failMode = (unsigned)((test >> 12) & 3u);
        if (failMode == 0) {
            ++sDiagAlphaKeep;
            if (((test >> 4) & 0xFFu) != 0) ++sDiagAlphaRefNonzero;
        } else {
            ++sDiagAlphaOther;
            ++sDiagAlphaOtherMode[failMode];
        }
    }
    /* The PSP reserves one shared scratch framebuffer, NOT a separate
       image for each offscreen GS FBP. When a different FBP writes to it,
       every earlier logical owner has lost its pixels. */
    if (sTargets[targetIndex].color == sVram + WORK_OFF &&
        sWorkOwnerFbp != (uint32_t)fbp) {
        int k;
        ++sFbWorkOwnerChanges;
        ++sFrameWorkOwners;
        for (k = 0; k < MAX_TARGETS; ++k) {
            if (k != targetIndex && sTargets[k].used &&
                sTargets[k].color == sVram + WORK_OFF) {
                sTargets[k].rendered = 0;
            }
        }
        sWorkOwnerFbp = (uint32_t)fbp;
    }
    /* Record that the image matches the current GS upload generation,
       so an upload over the same page makes subsequent sampling fall back
       to the actual decoded texture instead of stale framebuffer pixels. */
    sTargets[targetIndex].gen = gGsPageGen[(uint32_t)fbp & 511];
    sTargets[targetIndex].rendered = 1;
    /* GS coordinates 0..512 span the whole PSP width; height is 448. */
    sx = (float)SCR_W / 512.0f;
    sy = (float)SCR_H / 448.0f;

    if (sVbCount + n > MAX_QUEUE || (sVbCount != 0 && sVbMode != mode)) {
        flush();
    }
    {
        /* the state this primitive is drawn with: what changes ends the batch */
        /* Must match sSt exactly: the old shorter anonymous struct omitted
           tmode, amask and target, shifting every subsequent GPU state field. */
        __typeof__(sSt) n;
        uint64_t alpha = gGs.alpha[ctx];
        /* GS PRIM: bit 5 = FGE (fog), bit 6 = ABE (alpha blend).
           The old code read FGE here, so every text sprite generated by
           Font_BeginPacket (PRIM=0x156: ABE=1, FGE=0) was sent to GU with
           blending DISABLED. Transparent pixels then covered glyphs with
           solid rectangular blocks. Match the reference GS pixel() path
           which also tests (pr >> 6) & 1. */
        int abe = (int)((prim >> 6) & 1);
        int afunc = (int)((test >> 1) & 7);
        int zfunc = (int)((test >> 17) & 3);
        int tf = (int)((t0 >> 35) & 3);
        int filt = (int)((gGs.tex1[ctx] >> 5) & 1);
        int scx = (int)(sc & 0x7FF), scy = (int)((sc >> 32) & 0x7FF);

        memset(&n, 0, sizeof(n));
        n.tex = tex; n.tw = tw; n.th = th; n.fmt = fmt;
        n.target = sTargets[targetIndex].color;
        n.tmode = texIsTarget ? 0 : sTexSwizzled;
        /* The PS2 GS DECAL (TFX=1) REPLACES RGB with the texture.
           GU_TFX_DECAL incorrectly fades/mixes texture RGB with the
           fragment's RGB when TCC=RGBA. For alpha glyphs this made
           white ink grey; GU_TFX_REPLACE preserves the original GS
           texture RGB and correctly selects vertex vs texture alpha. */
        n.tfx = whiteSprite ? GU_TFX_REPLACE
                            : Psp_GsTexFuncToGu((unsigned)tf);
        n.tcc = (int)((t0 >> 34) & 1);
        n.wrapu = (cl & 3) ? GU_CLAMP : GU_REPEAT;
        n.wrapv = ((cl >> 2) & 3) ? GU_CLAMP : GU_REPEAT;
        n.filt = filt ? GU_LINEAR : GU_NEAREST;
        /* PS2 TEST: ATE/ATST/AREF/AFAIL. AFAIL=KEEP means a failing
           fragment must be discarded; the PSP's GU alpha test can
           implement that case directly. Other AFAIL modes need their
           own write-mask pass and are tracked separately.
           Critical: GU alpha compares (src & amask) with
           (ref & amask). The previous n.amask was always zero after
           memset(&n,0), so comparison happened against zero regardless
           of source alpha. This let invisible texels become opaque
           rectangular image tiles or rejected entire UI layers.
           Both sampled alpha AND AREF must use the same scale: PS2
           GS 0x80 = full opacity, PSP 0xFF = full opacity. */
        n.atest = (int)(test & 1) && ((test >> 12) & 3) == 0;
        n.afunc = afunc == 1 ? GU_ALWAYS : afunc == 2 ? GU_LESS : afunc == 3 ? GU_LEQUAL :
                  afunc == 4 ? GU_EQUAL : afunc == 5 ? GU_GEQUAL : afunc == 6 ? GU_GREATER : afunc == 7 ? GU_NOTEQUAL : GU_NEVER;
        n.aref = gPspAlphaFix
               ? (int)alpha_scale((unsigned)((test >> 4) & 0xFF))
               : (int)((test >> 4) & 0xFF);
        n.amask = gPspAlphaFix ? 0xFF : 0;
        n.pixelMask = 0u;
        n.stencilMode = hudMaskMode;
        if (Psp_GsHudMaskWritesStencil(hudMaskMode)) {
            /* PS2 FRAME.FBMSK 0x00FFFFFF masks all RGB writes but leaves
               the framebuffer alpha byte writable. PSP pixel mask +
               stencil replacement implements the same 1-bit coverage. */
            n.pixelMask = 0x00FFFFFFu;
            if (hudMaskMode == PSP_HUD_MASK_CLEAR) ++sHudMaskClear;
            else ++sHudMaskSet;
        } else if (hudMaskMode == PSP_HUD_MASK_DATE_ZERO) {
            ++sHudMaskDateZero;
        } else if (hudMaskMode == PSP_HUD_MASK_DATE_ONE) {
            ++sHudMaskDateOne;
        }
        n.ztest = (int)((test >> 16) & 1);
        n.zfunc = zfunc == 1 ? GU_ALWAYS : zfunc == 2 ? GU_GEQUAL : zfunc == 3 ? GU_GREATER : GU_NEVER;
        n.zwrite = ((int)((gGs.zbuf[ctx] >> 32) & 1)) ^ 1;
        if (n.ztest) ++sDiagZTestRequested;
        if (n.zwrite) ++sDiagZWriteRequested;
        if (topHudSprite) {
            if (n.ztest) ++sHudTopDepthRequested;
            if (n.zwrite) ++sHudTopDepthWriteRequested;
            /* The PSP keeps ONE physical depth surface, shared across
               two GS display FBPs and WORK. Do not let foreground 3D
               depth occlude the battle's top-anchored 2D overlay.
               No RGB, alpha, scissor, UV or texture changes. */
            if (gPspHudTopOverlay) {
                n.ztest = 0;
                n.zwrite = 0;
                n.zfunc = GU_ALWAYS;
                ++sHudTopDepthSuppressed;
            }
        }
        if (!gPspGsDepth) {
            /* state_apply ignores these three while gs_depth=0, so do not
               let a ZTE/ZMSK toggle alone end the vertex batch (the
               memcmp below): the picture is identical either way, and a
               fight toggles them between its 3D and HUD passes. */
            n.ztest = n.zfunc = n.zwrite = 0;
        }
        n.scx = scx; n.scy = scy;
        n.scw = (int)((sc >> 16) & 0x7FF) + 1 - scx;
        n.sch = (int)((sc >> 48) & 0x7FF) + 1 - scy;
        {
            /* the blend: ((A - B) * C >> 7) + D, written into the same fields blend_set fills */
            int A = (int)(alpha & 3), B = (int)((alpha >> 2) & 3), C = (int)((alpha >> 4) & 3), D = (int)((alpha >> 6) & 3);
            int k[2], i, fix = (int)((alpha >> 32) & 0xFF);
            int guFix = gPspGsFixScale ? (int)Psp_GsFixToGu((unsigned)fix) : fix;

            n.blend_on = abe;
            if (abe && C == 2) {
                /* GS FIX=0x80 represents full opacity. PSP must use 0xFF.
                   Track text/HUD sprites separately to confirm reachability. */
                ++sDiagFixUsed;
                if (type == 6) {
                    ++sDiagSpriteFixUsed;
                    if (fix >= 0x80) ++sDiagSpriteFixOpaque;
                }
                ++sDiagFixRange[fix == 0 ? 0 : fix < 64 ? 1 : fix < 128 ? 2 : 3];
            }
            n.blendop = GU_ADD;
            for (i = 0; i < 2; i++) {
                int c = (A == i) - (B == i), one = (D == i);
                k[i] = c == 0 ? one : c > 0 ? (one ? 5 : 2) : (one ? 4 : 3);
                if (k[i] == 3) { n.blendop = i == 0 ? GU_REVERSE_SUBTRACT : GU_SUBTRACT; }
            }
            for (i = 0; i < 2; i++) {
                int c = k[i] == 3 ? 2 : k[i], f;
                if (c == 0) { f = GU_FIX; if (i == 0) { n.sfix = 0; } else { n.dfix = 0; } }
                else if (c == 1 || c == 5) { f = GU_FIX; if (i == 0) { n.sfix = 255; } else { n.dfix = 255; } }
                else if (c == 2) {
                    if (C == 0) { f = GU_SRC_ALPHA; }
                    else if (C == 1) { f = GU_DST_ALPHA; }
                    else { f = GU_FIX; if (i == 0) { n.sfix = guFix; } else { n.dfix = guFix; } }
                } else {
                    if (C == 0) { f = GU_ONE_MINUS_SRC_ALPHA; }
                    else if (C == 1) { f = GU_ONE_MINUS_DST_ALPHA; }
                    else { f = GU_FIX; if (i == 0) { n.sfix = 255 - guFix; } else { n.dfix = 255 - guFix; } }
                }
                if (i == 0) { n.src = f; } else { n.dst = f; }
            }
        }
        if (memcmp(&n, &sSt, sizeof(n)) != 0) {
            flush();
            memcpy(&sSt, &n, sizeof(n));
        }
    }
    d = &sVb[sVbCount];
    sVbMode = mode;
    if (tme) {
        extern volatile uint64_t gGsClutT0;
        if (((t0 >> 61) & 7u) == 0 && ((t0 >> 37) & 0x3FFFu) == 0 &&
            Gs_PsmBits((uint32_t)((t0 >> 20) & 0x3Fu)) <= 8 && gGsClutT0 != 0)
            ++sDiagClutReuseCandidate;
        ++sDiagClampS[cl & 3u];
        ++sDiagClampT[(cl >> 2) & 3u];
        ++sDiagTfx[(t0 >> 35) & 3u];
        if (type == 6 && ((t0 >> 35) & 3u) == 1u) {
            if ((t0 >> 34) & 1u) ++sDiagSpriteDecalRgba;
            else ++sDiagSpriteDecalRgb;
        }
        if (type == 6 && ((t0 >> 35) & 3u) >= 2u)
            ++sDiagSpriteHighlight;
        if (!((t0 >> 34) & 1u)) ++sDiagTccRgb;
    }
    /* Record actual GS ABE, never the neighboring FGE (fog) bit. */
    if ((prim >> 6) & 1u) ++sDiagBlendOn;
    if (type == 6) {
        if (whiteSprite) ++sDiagWhiteSpriteDraws;
        ++sDiagFlatSprites;
        if (v[0].r != v[1].r || v[0].g != v[1].g ||
            v[0].b != v[1].b || v[0].a != v[1].a)
            ++sDiagUnequalSpriteColors;
    }
    for (i = 0; i < n; i++) {
        const GsVertex *s = &v[i];
        /* The PS2 GS flat-shades sprites from the SECOND vertex, even
           when PRIM.IIP is set. With IIP=0 the last vertex supplies
           colour for triangles/lines as well. The PSP GU interpolates
           independently provided RGBA across GU_SPRITES; feeding the
           first vertex's alpha could fade or blacken an entire picture
           atlas rectangle. UV/position still come from each endpoint.
           gs_core.c's draw_sprite() uses vcol(b) as the reference. */
        const GsVertex *shade = (gPspFlatSprite &&
                               (type == 6 || !(prim & (1ull << 3))))
                             ? &v[n - 1] : s;
        float u, vv;
        /* The PSP GU's 2D path consumes UV in TEXELS, not normalized UV.
           sceGuTexScale only applies to GU_TRANSFORM_3D and cannot fix the
           old 0..1 UVs used with GU_TRANSFORM_2D. This previously sampled
           almost one texel for an entire sprite (solid-colour blocks).
           PS2 FST: 12.4 texel coordinates; ST: normalized / perspective. */
        if (fst) {
            u = (float)s->u / 16.0f * lodU;
            vv = (float)s->v / 16.0f * lodV;
        } else {
            float q = s->q != 0.0f ? s->q : 1.0f;
            u = (s->s / q) * (float)(tw ? tw : 1);
            vv = (s->t / q) * (float)(th ? th : 1);
        }
        d[i].u = u * us;
        d[i].v = vv * vs;
        {
            /* PS2 GS TFX=MODULATE multiplies texture RGB by vertex RGB / 128
               (see shade() in gs_core.c). PSP GU_TFX_MODULATE divides by
               255 instead. Convert the vertex's RGB gain only for this mode;
               solid-colour draws and other TFX modes must keep GS RGB. */
            unsigned r = shade->r, g = shade->g, b = shade->b;
            if (tme && ((t0 >> 35) & 3) == 0) {
                r = alpha_scale(r);
                g = alpha_scale(g);
                b = alpha_scale(b);
            }
            d[i].c = r | (g << 8) | (b << 16) | (alpha_scale(shade->a) << 24);
        }
        d[i].x = s->x * sx;
        d[i].y = s->y * sy;
        d[i].z = (float)Psp_GsDepthZ24ToGuZ16(s->z); /* GS XYZ2 Z24 -> PSP GU Z16 */
    }
    sVbCount += n;
    if (sVbCount + 4 > MAX_QUEUE) {
        flush();
    }
    if (profSample) {
        sProfDrawSampleUs += Psp_TimeUs() - profStart;
        ++sProfDrawSamples;
    }
}

/* Reused by the stage and fighter native VU1 renderers. Defined after
   the fighter adapter to preserve the original GS->GU dispatch order. */
typedef struct PspStageFastBatch {
    int active;
    PspStageTriScale scale;
} PspStageFastBatch;
static void Psp_NativeEmitTriangle(PspStageFastBatch *b, int ctx,
                                   const GsVertex tri[3], int fighter);

/* The VU1 program paths: the PSP runs the programs through the interpreter (BT3_VU_INTERP), so these are only
   reached if that is switched off. */
/* Accept the ENTIRE animated fighter strip or return to interpreter.
   This stage executes before hle_program0/1 change TEX0 or PRIM.
   The PSP GS path is already affine-UV for interpreted and native
   strips; differences in W do not alter its per-vertex ST/Q mapping. */
int Psp_FighterPrepare(const float *vertices, uint32_t count,
                       const float *consts, int fade) {
    uint32_t i;
    sFighterPreparedCount = 0;
    ++sFrameFighterInputs;
    if (!gPspNativeFighter || !sReady || !vertices || !consts ||
        count < 3 || count > 38) return 0;
    for (i = 0; i < count; ++i) {
        const float *v = vertices + i*12u;
        PspFighterVertex *out = &sFighterPrepared[i];
        if (!PspFighter_Transform(out,v,v+4,v+8,consts,fade)) {
            ++sFighterNativeFallback;
            return 0;
        }
        /* Perspective-varying Q is fine for the existing PSP GS 2D
           adapter: it computes ST/Q per vertex in BOTH native and
           interpreted paths, then submits the same affine UV. We still
           reject all invalid, clipped and non-finite vertices above. */
    }
    sFighterPreparedCount = count;
    sFighterPreparedFade = fade;
    return 1;
}

void GsGpu_DrawVu0(int layer, int ctx, const float *vertices,
                   uint32_t count, const float *consts) {
    uint32_t i;
    GsVertex tri[3];
    PspStageFastBatch fast = {0};
    float ox, oy;
    unsigned char r, g, b, a;
    int isCel = (layer == 1 || layer == 5);
    (void)vertices;
    if (!gPspNativeFighter || !consts ||
        sFighterPreparedCount != count || ctx < 0 || ctx > 1 ||
        (isCel && sFighterPreparedFade != (layer == 5))) return;
    ox = (float)(gGs.xyoffset[ctx] & 0xFFFFu) / 16.0f;
    oy = (float)((gGs.xyoffset[ctx] >> 32) & 0xFFFFu) / 16.0f;
    r = PspStage4_Color(consts[(isCel ? 23u : 22u)*4u]);
    g = PspStage4_Color(consts[(isCel ? 23u : 22u)*4u+1u]);
    b = PspStage4_Color(consts[(isCel ? 23u : 22u)*4u+2u]);
    a = PspStage4_Color(consts[(isCel ? 23u : 22u)*4u+3u]);
    for (i = 0; i + 2 < count; ++i) {
        uint32_t j;
        for (j = 0; j < 3; ++j) {
            uint32_t index = i + j;
            const PspFighterVertex *v;
            if ((i & 1u) && j < 2) index = i + 1u - j;
            v = &sFighterPrepared[index];
            memset(&tri[j], 0, sizeof(tri[j]));
            tri[j].x = v->x - ox;
            tri[j].y = v->y - oy;
            tri[j].z = v->z;
            tri[j].s = (isCel ? v->s1 : v->s0) * v->q;
            tri[j].t = (isCel ? v->t1 : v->t0) * v->q;
            tri[j].q = v->q;
            tri[j].r = r; tri[j].g = g; tri[j].b = b; tri[j].a = a;
        }
        Psp_NativeEmitTriangle(&fast, ctx, tri, 1);
    }
    /* Both program 0 and 1 have two layers. The cache must live until
       the caller draws the second one; a following batch clears it. */
    if (layer == 1 || layer == 5) {
        ++sFighterNativeBatches;
        sFighterNativeVerts += count;
    }
}
/* Validate and transform an entire stage strip BEFORE modifying any
   GS register or submitting geometry. If perspective/clipping cannot be
   represented faithfully by PSP 2D GU, the caller runs the PS2 VU1
   interpreter instead. No partial batch may be committed here. */
extern int gPspNativeStageClip;
static int Psp_Stage4PrepareDirect(int ctx, const float *vertices,
                                   uint32_t count, const float *consts) {
    uint32_t i;
    float ox, oy;
    sStage4PreparedCount = 0;
    ++sFrameStageInputs;
    if (!gPspNativeStage4 || count < 3 || count > 1024 ||
        !sReady || ctx < 0 || ctx > 1 || !vertices || !consts) return 0;
    ox = (float)(gGs.xyoffset[ctx] & 0xFFFFu) / 16.0f;
    oy = (float)((gGs.xyoffset[ctx] >> 32) & 0xFFFFu) / 16.0f;
    for (i = 0; i < count; ++i) {
        const float *p = vertices + i * 12u;
        PspStage4Vertex *o = &sStage4Prepared[i];
        PspStageClipVertex *clip = &sStage4ClipSource[i];
        if (!PspStage4_ClipInput(clip, p, p + 4, p + 8, consts + 16)) {
            ++sNativeStageFallback;
            ++sNativeStageClipFailed;
            return 0;
        }
        sStage4ClipFlag[i] = clip->mask;
        if (clip->mask) {
            /* The old native path rejected an entire strip if ONE
               vertex was outside the frustum. This safe path clips
               individual triangles instead of rerunning all VU1. */
            if (!gPspNativeStageClip) {
                ++sNativeStageFallback;
                return 0;
            }
        } else if (!PspStage4_Transform(o, p, p + 4, p + 8,
                                         consts, ox, oy)) {
            ++sNativeStageFallback;
            ++sNativeStageClipFailed;
            return 0;
        }
    }
    /* Never commit GS state if a clipped triangle would be invalid.
       Clipped batches must be checked before hle_program4 writes TEX0. */
    for (i = 0; i + 2 < count; ++i) {
        unsigned idx0 = (i & 1u) ? i + 1u : i;
        unsigned idx1 = (i & 1u) ? i : i + 1u;
        unsigned idx2 = i + 2u;
        if (sStage4ClipFlag[idx0] || sStage4ClipFlag[idx1] ||
            sStage4ClipFlag[idx2]) {
            PspStageClipVertex input[3], poly[PSP_STAGE_CLIP_MAX];
            int n, j;
            input[0] = sStage4ClipSource[idx0];
            input[1] = sStage4ClipSource[idx1];
            input[2] = sStage4ClipSource[idx2];
            n = PspStage4_ClipTriangle(poly, input);
            if (n < 0) {
                ++sNativeStageFallback;
                ++sNativeStageClipFailed;
                return 0;
            }
            for (j = 0; j < n; ++j) {
                PspStage4Vertex probe;
                if (!PspStage4_ClipProject(&probe, &poly[j],
                                           consts + 32, ox, oy)) {
                    ++sNativeStageFallback;
                    ++sNativeStageClipFailed;
                    return 0;
                }
            }
        }
    }
    sStage4PreparedCount = count;
    return 1;
}

/* A rejected stage strip previously ALWAYS entered the 399-instruction
   microprogram, even if all its triangles were outside the camera.
   Prove complete invisibility instead of executing the VU1 clipper for
   those strips. Preserve the original interpreter for every ambiguous
   case and keep the exact GIF/TEX0/PRIM writes in hle_program4(). */
extern int gPspNativeStageCull;
int Psp_Stage4Prepare(int ctx, const float *vertices, uint32_t count,
                      const float *consts) {
    unsigned oldFallback = sNativeStageFallback;
    unsigned oldClipFailed = sNativeStageClipFailed;
    sStage4Culled = 0;
    if (Psp_Stage4PrepareDirect(ctx, vertices, count, consts))
        return 1;
    if (gPspNativeStageCull && gPspNativeStage4 && sReady &&
        ctx >= 0 && ctx <= 1 && vertices && consts &&
        PspStage4_EntireStripOutside(vertices, count, consts + 16)) {
        /* The direct validator rejected before changing GS state.
           This batch will NOT invoke VU1, so do not count it as a
           native rejection. hle_program4 still writes the GS registers
           and calls DrawVu4, which safely emits zero triangles. */
        sNativeStageFallback = oldFallback;
        sNativeStageClipFailed = oldClipFailed;
        sStage4PreparedCount = count;
        sStage4Culled = 1;
        ++sNativeStageCulledStrips;
        sNativeStageCulledVerts += count;
        return 1;
    }
    return 0;
}

/* The normal GS draw still executes first, verifying texture/alpha/depth/
   scissor and selecting the proper screen/work target. Within one VU1
   program-4 strip, the game does not change any of those registers.
   Re-emitting the remaining triangles using the established GU state
   avoids hashing texture keys, rebinding targets and comparing a full
   state structure for every triangle. Preserve strip triangle order,
   winding, flat-shading and the existing affine ST/Q behavior. */
static void Psp_NativeEmitTriangle(PspStageFastBatch *b, int ctx,
                                   const GsVertex tri[3], int fighter) {
    if (b->active) {
        int i;
        if (sVbCount + 3 > MAX_QUEUE) flush();
        if (sVbCount && sVbMode != GU_TRIANGLES) flush();
        sVbMode = GU_TRIANGLES;
        PspStageTriPack(&sVb[sVbCount], tri, &b->scale);
        sVbCount += 3;
        /* Preserve real draw counts, but the GS draw profiler only
           measures slow-path calls; see the separate fast count. */
        ++sDraws;
        if (fighter) ++sNativeFighterFastTriangles;
        else ++sNativeStageFastTriangles;
        ++sDiagZTri;
        if ((gGs.test[ctx] >> 16) & 1u) {
            ++sDiagZTriTest;
            ++sDiagZFunc[(gGs.test[ctx] >> 17) & 3u];
        }
        if (!((gGs.zbuf[ctx] >> 32) & 1u)) ++sDiagZTriWrite;
        if (tri[0].z == tri[1].z && tri[1].z == tri[2].z)
            ++sDiagZTriFlat;
        for (i=0; i<3; ++i) {
            if (tri[i].z < sDiagZMin) sDiagZMin = tri[i].z;
            if (tri[i].z > sDiagZMax) sDiagZMax = tri[i].z;
        }
        if (sSt.target == sVram + WORK_OFF) {
            ++sDiagZTriWork; ++sDiagDrawWork; ++sFrameWorkDraws;
            ++sPairedWorkDraws;
        } else {
            ++sDiagDrawMain; ++sFrameScreenDraws;
            ++sPairedScreenDraws;
        }
        if (sVbCount + 4 > MAX_QUEUE) flush();
        return;
    }
    {
        unsigned skipped = sSkipped, draws = sDraws;
        int i, alias = 0;
        GsGpu_Draw(3, ctx, tri);
        /* Only the default flat-color path has verified equivalence
           in PspStageTriPack; nondefault flat_sprite=0 stays slow. */
        if (!(fighter ? gPspNativeFighterBatch : gPspNativeStageBatch) ||
            !gPspFlatSprite || sSkipped != skipped ||
            sDraws != draws + 1 || sVbMode != GU_TRIANGLES ||
            !sReady || sSt.stencilMode != PSP_HUD_MASK_NONE ||
            sSt.pixelMask != 0u ||
            (gGs.test[ctx] & (1ull << 14)))
            return;
        /* Valid framebuffer-as-texture has independent scaling
           (s->s/q is multiplied by 512 plus alias factors).
           Keep the normal path for such special GS effects. */
        if (sSt.tex) {
            for (i=0; i<MAX_TARGETS; ++i)
                if (sTargets[i].used && sSt.tex == sTargets[i].color)
                    alias=1;
            if (alias) return;
        }
        b->scale = PspStageTriMakeScale(gGs.prim, gGs.tex0[ctx],
                                        sSt.tw, sSt.th);
        b->active = 1;
        if (fighter) ++sNativeFighterFastStarts;
        else ++sNativeStageFastStarts;
    }
}

void GsGpu_DrawVu4(int ctx, const float *vertices, uint32_t count,
                   const float *consts) {
    uint32_t i;
    GsVertex tri[3];
    PspStageFastBatch fast = {0};
    (void)vertices; (void)consts;
    if (sStage4PreparedCount != count || !gPspNativeStage4)
        return; /* only reached after Psp_Stage4Prepare accepts a strip */
    if (sStage4Culled) {
        sStage4Culled = 0;
        sStage4PreparedCount = 0;
        return; /* framebuffer remains untouched, but GS state was kept */
    }
    for (i = 0; i + 2 < count; ++i) {
        unsigned idx[3] = {(i & 1u) ? i + 1u : i,
                           (i & 1u) ? i : i + 1u, i + 2u};
        uint32_t j;
        if (sStage4ClipFlag[idx[0]] || sStage4ClipFlag[idx[1]] ||
            sStage4ClipFlag[idx[2]]) {
            PspStageClipVertex input[3], poly[PSP_STAGE_CLIP_MAX];
            PspStage4Vertex output[PSP_STAGE_CLIP_MAX];
            float ox = (float)(gGs.xyoffset[ctx] & 0xFFFFu) / 16.0f;
            float oy = (float)((gGs.xyoffset[ctx] >> 32) & 0xFFFFu) / 16.0f;
            int n, k;
            for (j = 0; j < 3; ++j) input[j] = sStage4ClipSource[idx[j]];
            n = PspStage4_ClipTriangle(poly, input);
            if (n == 0) {
                ++sNativeStageClipOutside;
                continue;
            }
            /* All output vertices were preflighted by Prepare. */
            if (n < 0) continue;
            for (k = 0; k < n; ++k) {
                if (!PspStage4_ClipProject(&output[k], &poly[k],
                                            consts + 32, ox, oy)) {
                    n = 0; /* never submit an invalid partial polygon */
                    break;
                }
            }
            for (k = 1; k + 1 < n; ++k) {
                unsigned ids[3] = {0u, (unsigned)k, (unsigned)(k + 1)};
                for (j = 0; j < 3; ++j) {
                    const PspStage4Vertex *v = &output[ids[j]];
                    memset(&tri[j], 0, sizeof(tri[j]));
                    tri[j].x = v->x; tri[j].y = v->y; tri[j].z = v->z;
                    tri[j].s = v->s; tri[j].t = v->t; tri[j].q = v->q;
                    tri[j].r = v->r; tri[j].g = v->g;
                    tri[j].b = v->b; tri[j].a = v->a;
                }
                Psp_NativeEmitTriangle(&fast, ctx, tri, 0);
                ++sNativeStageClipTriangles;
            }
            continue;
        }
        for (j = 0; j < 3; ++j) {
            const PspStage4Vertex *v = &sStage4Prepared[idx[j]];
            memset(&tri[j], 0, sizeof(tri[j]));
            tri[j].x = v->x;
            tri[j].y = v->y;
            tri[j].z = v->z;
            tri[j].s = v->s;
            tri[j].t = v->t;
            tri[j].q = v->q;
            tri[j].r = v->r;
            tri[j].g = v->g;
            tri[j].b = v->b;
            tri[j].a = v->a;
        }
        Psp_NativeEmitTriangle(&fast, ctx, tri, 0);
    }
    ++sNativeStageBatches;
    sNativeStageVerts += count;
    sStage4PreparedCount = 0;
}
void GsGpu_DrawVu6(int ctx, const float *vertices, uint32_t count, const float *consts) {
    (void)ctx; (void)vertices; (void)count; (void)consts;
}

/* The game is about to fill a frame buffer from the processor (the fades: the shown picture read back and put
   into two buffers). What it will hold is the picture on the screen, so that is what is put there. */
void GsGpu_FbUpload(int second) {
    if (!sReady) {
        return;
    }
    /* The original GS path reads back framebuffer pixels to an image
       that is later used in fade/effects passes. PSP currently has NO
       equivalent readback/copy here: flush() only submits queued draws.
       Log reachability without claiming the effect has been ported. */
    ++sDiagUnimplementedFbUpload;
    flush();
    (void)second;
}

void GsGpu_Native(int effect) {
    /* Native post-processing is not implemented on the PSP. */
    ++sDiagUnimplementedNativeEffect;
    (void)effect;
}

void GsGpu_FrameEnd(void) {
    unsigned long long t0;
    if (!sReady) {
        return;
    }
    ++sProfFrameEnds;
    flush();
    /* Restore the screen target even if the last GS pass used a work FBP.
       The movie overlay and next presentation must not land in scratch VRAM. */
    sceGuDrawBufferList(GU_PSM_5551, (void *)(uintptr_t)sMainDrawOffset, BUF_W);
    movie_draw_frame();
    t0 = Psp_TimeUs();
    sceGuFinish();
    sceGuSync(0, 0);
    sProfGeSyncUs += Psp_TimeUs() - t0;
    /* Port_VBlank in plat_stub.c already waits on the game's 59.94 Hz
       clock (1 tick in menus, 2 ticks in battles). An unconditional
       sceDisplayWaitVblankStart here can add a second frame delay and
       cut display throughput even when the GPU work finished promptly.
       Test the single-clock path without modifying game logic/speed.
       Restorable with swap_vsync=1 in bt3.txt. */
    if (gPspSwapVsync) {
        t0 = Psp_TimeUs();
        sceDisplayWaitVblankStart();
        sProfVblankUs += Psp_TimeUs() - t0;
    }
    /* Report the *same PSP frame*'s GS groups before the next swap.
       In the fast-forward recording, some frames show only the stage,
       while others show fighters/HUD on black.  This report distinguishes
       separate GS chains from wrong final draw target / clearing. */
    if ((sFrameStageInputs || sFrameFighterInputs) &&
        (sFrameDiagShown < 40u || sFrame - sFrameLastDiag >= 30u)) {
        Psp_Log("psp: frame composition: frame=%u GS-main-fbp=%d stage-input=%u fighter-input=%u screen-draw=%u work-draw=%u work-owner-changes=%u old-draw=0x%x\n",
                sFrame, gGsMainFbp, sFrameStageInputs, sFrameFighterInputs,
                sFrameScreenDraws, sFrameWorkDraws, sFrameWorkOwners,
                sMainDrawOffset);
        sFrameLastDiag = sFrame;
        ++sFrameDiagShown;
    }
    if (sPairedThisFrame) ++sPairedDisplayFrames;
    sPairedThisFrame = 0;
    sFrameStageInputs = sFrameFighterInputs = 0;
    sFrameScreenDraws = sFrameWorkDraws = sFrameWorkOwners = 0;
    {
        /* sceGuSwapBuffers() explicitly returns the new PSP drawbuffer
           according to PSPSDK. Don't assume a 0/FB_SIZE flip if the GE
           (or PPSSPP) has chosen otherwise.  If it is not one of our
           two recognized buffers, retain the original deterministic
           flip.  swap_from_sdk=0 restores the legacy behavior. */
        void *selected = sceGuSwapBuffers();
        unsigned fallback = sMainDrawOffset == FB0_OFF ? FB1_OFF : FB0_OFF;
        int actual = Psp_GuSwapOffset((uintptr_t)selected, FB_SIZE);
        if (actual >= 0) {
            if ((unsigned)actual != fallback) {
                ++sGuSwapMismatch;
                if (sGuSwapMismatch <= 8)
                    Psp_Log("psp: GU swap mismatch: returned=%p normalized=0x%x predicted=0x%x (sync=%d)\n",
                            selected, (unsigned)actual, fallback, gPspSwapFromSdk);
            }
            sMainDrawOffset = gPspSwapFromSdk ? (unsigned)actual : fallback;
        } else {
            ++sGuSwapUnknown;
            if (sGuSwapUnknown <= 3)
                Psp_Log("psp: GU swap returned non-FB pointer %p; keep predicted offset=0x%x\n",
                        selected, fallback);
            sMainDrawOffset = fallback;
        }
    }
    sFrame++;
    sceGuStart(GU_DIRECT, sList);
    /* The previous list may have ended on a limited GS scissor.
       Clear must cover the ENTIRE next draw buffer, not only that
       scissor rectangle. And sceGuDrawBufferList in the preceding
       list pointed to the PRE-SWAP draw buffer. Rebind the new one
       explicitly, otherwise old movie frames can remain visible. */
    sceGuDrawBufferList(GU_PSM_5551, (void *)(uintptr_t)sMainDrawOffset, BUF_W);
    /* Never allow the previous frame's alpha-only GS HUD pass to mask
       the clear or next frame's battle geometry. */
    sceGuDisable(GU_STENCIL_TEST);
    sceGuPixelMask(0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuClearColor(0);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    memset(&sSt, 0, sizeof(sSt));
    sAppliedValid = 0; /* previous list included clear / optional movie */
    gPspGuFlushes++;
}

int Psp_GuInit(void) {
    /* The PSP debug screen used by Psp_LoadGameData writes straight into
       framebuffer VRAM. Previously only the first GU draw buffer was
       cleared; the second/display buffer could still hold its boot text,
       leaking into the battle scene after swap or framebuffer aliasing.
       Clear both real screen buffers ONCE before starting the GE.
       Do not clear them later: doing so would erase pending draws.
       The separate work surface also starts empty. */
    memset(sVram + FB0_OFF, 0, FB_SIZE);
    memset(sVram + FB1_OFF, 0, FB_SIZE);
    memset(sVram + WORK_OFF, 0, WORK_SIZE);
    Psp_Log("psp: startup VRAM clean: both screen buffers and work target cleared (one-time)\n");
    Psp_Log("psp: GS depth Z24->Z16 direct conversion active [depth-z16-20261026]\n");
    Psp_Log("psp: guarded texture reuse [texepoch-20261026] enabled\n");
    Psp_Log("psp: GS compositor frame/swap trace [frame-composition-20261026], swap_from_sdk=%d\n",
            gPspSwapFromSdk);
    Psp_Log("psp: GS display-pair routing experiment [scene-paired-fbp-20261026], scene_pair_screen=%d\n",
            gPspScenePairScreen);
    Psp_Log("psp: battle top HUD depth isolation [hud-overlay-20261026], hud_top_overlay=%d\n",
            gPspHudTopOverlay);
    Psp_Log("psp: life gauge FBMSK/FBA and TEST.DATE stencil translation [hud-stencil-20261026], hud_mask_fix=%d\n",
            gPspHudMaskFix);
    Psp_Log("psp: per-stage-strip GS triangle batching [stage-gs-batch-20261026] native_stage_batch=%d\n",
            gPspNativeStageBatch);
    Psp_Log("psp: fighter dual-layer triangle batching [fighter-gs-batch-20261026] native_fighter_batch=%d\n",
            gPspNativeFighterBatch);
    sMainDrawOffset = FB0_OFF;
    sVb = malloc(MAX_QUEUE * sizeof(Vtx));
    if (sVb == NULL) {
        return 0;
    }
    /* Reserve predictable scratch space before the game's textures
       fill the newlib heap. In the old code these allocations failed
       repeatedly at runtime, evicting GS textures under GPU fences.
       Optional reservations fail gracefully to the old malloc path;
       reserved bytes are removed from the texture-cache RAM budget. */
    if (gPspTextureScratch) {
        sScratchRgba = (uint32_t *)malloc(PSP_TEX_SCRATCH_RGBA_BYTES);
        if (sScratchRgba) sScratchReservedBytes += PSP_TEX_SCRATCH_RGBA_BYTES;
        sScratchIndices = (uint8_t *)malloc(PSP_TEX_SCRATCH_INDEX_BYTES);
        if (sScratchIndices) sScratchReservedBytes += PSP_TEX_SCRATCH_INDEX_BYTES;
        sTexRamBudget = Psp_TexRamBudget(MAX_RAM_TEX_BYTES, sScratchReservedBytes);
    }
    Psp_Log("psp: shared texture scratch: rgba=%u KB idx=%u KB reserve=%u KB RAM-cache-limit=%u KB enabled=%d\n",
            sScratchRgba ? PSP_TEX_SCRATCH_RGBA_BYTES/1024u : 0u,
            sScratchIndices ? PSP_TEX_SCRATCH_INDEX_BYTES/1024u : 0u,
            sScratchReservedBytes/1024u, sTexRamBudget/1024u,
            gPspTextureScratch);
    sceGuInit();
    sceGuStart(GU_DIRECT, sList);
    /*
     * sceGuDrawBuffer/DispBuffer/DepthBuffer take GE VRAM offsets, not
     * CPU-addressable VRAM pointers. Passing 0x04000000-based pointers here
     * can make the GE render into the wrong location or leave the screen black.
     * Keep sVram for CPU-side texture/readback access only.
     */
    sceGuDrawBuffer(GU_PSM_5551, (void *)(uintptr_t)FB0_OFF, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)(uintptr_t)FB1_OFF, BUF_W);
    sceGuDepthBuffer((void *)(uintptr_t)DEPTH_OFF, BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DITHER);
    sceGuClearColor(0);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
    sReady = 1;
    sFrame = 1;
    sAppliedValid = 0;
    sceGuStart(GU_DIRECT, sList);
    return 1;
}

/* The renderer's front end asks the back end to start (GR_INIT, gs_core.c). The engine is already running by
   then (Psp_GuInit, before the game's thread): there is nothing left to set up. 1: the back end will draw. */
int GsGpu_Init(void) {
    return sReady;
}

/* The fighters' shadows (src/sys/gfxm_e_b.c): the Vita keeps each shadow's silhouette in a place of its own
   and draws the shadow with the silhouette it made before, one picture late. The PSP draws no silhouette at
   all (the engine has no room for another pass over the fighter), so there is never one to reuse. */
int Port_ShadowSil(int slot) {
    (void)slot;
    return 0;
}
void Port_ShadowSilEnd(void) {
}

/* Counters and switches the front end and the game read and write for their reports, their profiling and their
   Vita-only paths. On the Vita they are defined where the code they measure is (gs_gpu.c, vita_vgl.c, eft_c.c,
   all in Vita-only blocks); the PSP has no such code, but the readings stay, so the storage must exist. */
uint64_t gGpuTexNs, gGpuPipeNs, gGpuEndNs;
unsigned gGpuNewTex, gGpuNewTexPixels, gGpuNewPipes;
volatile unsigned char gProfGame;
int gVglSamePos;
int gVitaGsSkip, gVitaGsSkipR; /* (gs_core.c renames gVitaGsSkip to gVitaGsSkipR on the consoles) */
unsigned gVitaOtRun[6];
volatile int gVitaShotMark;
unsigned gVitaSurfN[10];
unsigned long long gVitaSurfUs[3];
unsigned long long gVitaSurfCyc[8];

/* For the log (psp_main.c). */
unsigned Psp_GuFramesPresented(void) {
    /* One per completed GS frame, NOT per display refresh. This remains
       near 30 in the original battle loop even at full 100% speed. */
    return sFrame > 0 ? sFrame - 1 : 0;
}

void Psp_GuStats(unsigned out[4]) {
    out[0] = sDraws;
    out[1] = sBatches;
    out[2] = sNewTex;
    out[3] = sSkipped;
    Psp_Log("psp: native fighter VU1: batches=%u vertices=%u rejected=%u enabled=%d (2-bone skin, cel/fade)\n",
            sFighterNativeBatches,sFighterNativeVerts,
            sFighterNativeFallback,gPspNativeFighter);
    sFighterNativeBatches = sFighterNativeVerts = sFighterNativeFallback = 0;
    Psp_Log("psp: native stage VU1 program4: batches=%u vertices=%u conservative-fallbacks=%u enabled=%d\n",
            sNativeStageBatches, sNativeStageVerts, sNativeStageFallback,
            gPspNativeStage4);
    sNativeStageBatches = sNativeStageVerts = sNativeStageFallback = 0;
    Psp_Log("psp: stage GS triangle batching: starts=%u fast-triangles=%u enabled=%d (regular GsGpu_Draw handles first triangle per strip)\n",
            sNativeStageFastStarts, sNativeStageFastTriangles, gPspNativeStageBatch);
    sNativeStageFastStarts = sNativeStageFastTriangles = 0;
    Psp_Log("psp: fighter GS triangle batching: starts=%u fast-triangles=%u enabled=%d (preserves 2-layer VU1 color/texture)\n",
            sNativeFighterFastStarts, sNativeFighterFastTriangles,
            gPspNativeFighterBatch);
    sNativeFighterFastStarts = sNativeFighterFastTriangles = 0;
    Psp_Log("psp: stage full-frustum trivial reject: strips=%u vertices=%u enabled=%d (no GS pixels, no VU1 interpreter)\n",
            sNativeStageCulledStrips, sNativeStageCulledVerts, gPspNativeStageCull);
    sNativeStageCulledStrips = sNativeStageCulledVerts = 0;
    Psp_Log("psp: stage native CPU clipping: triangles=%u wholly-outside=%u preflight-rejected=%u enabled=%d\n",
            sNativeStageClipTriangles, sNativeStageClipOutside, sNativeStageClipFailed,
            gPspNativeStageClip);
    sNativeStageClipTriangles = sNativeStageClipOutside = sNativeStageClipFailed = 0;
    Psp_Log("psp: architecture gaps: unimplemented-fb-readbacks=%u native-effects=%u (no pixels copied for FB readback)\n",
            sDiagUnimplementedFbUpload, sDiagUnimplementedNativeEffect);
    sDiagUnimplementedFbUpload = sDiagUnimplementedNativeEffect = 0;
    Psp_Log("psp: GS PSMCT16 colour output: native=%u legacy-skipped=%u option=%d (16S/depth still unsupported)\n",
            sDiagFb16Native, sDiagSkip16, gPspGsFb16);
    sDiagFb16Native = 0;
    Psp_Log("psp: GS menu passes: screen=%u work=%u skipped_fb16=%u skipped_depth=%u skipped_special_texture=%u missing_texture=%u no_target=%u\n",
            sDiagDrawMain, sDiagDrawWork, sDiagSkip16, sDiagSkipDepth,
            sDiagSkipPsm, sDiagSkipTexMem, sDiagSkipNoTarget);
    Psp_Log("psp: GS high-index textures: permitted=%u blocked=%u decode-failed=%u gs_hi_tex=%d\n",
            sDiagHiTexAllowed, sDiagHiTexBlocked, sDiagHiTexDecodeFailed, gPspGsHiTex);
    sDiagHiTexAllowed = sDiagHiTexBlocked = sDiagHiTexDecodeFailed = 0;
    Psp_Log("psp: GS alpha: KEEP=%u (AREF!=0: %u), other-AFAIL=%u (FB_ONLY=%u ZB_ONLY=%u RGB_ONLY=%u), alpha_fix=%d\n",
            sDiagAlphaKeep, sDiagAlphaRefNonzero, sDiagAlphaOther,
            sDiagAlphaOtherMode[1], sDiagAlphaOtherMode[2],
            sDiagAlphaOtherMode[3], gPspAlphaFix);
    Psp_Log("psp: GS HUD alpha mask: mode=%d clear=%u write-1=%u DATE-alpha0=%u DATE-alpha1=%u full-frame-alpha-clear-strips=%u sweeps=%u (two 16-strip resets per battle frame)\n",
            gPspHudMaskFix, sHudMaskClear, sHudMaskSet,
            sHudMaskDateZero, sHudMaskDateOne,
            sHudMaskFullClearStrips, sHudMaskFullClearSweeps);
    sHudMaskClear = sHudMaskSet = sHudMaskDateZero = sHudMaskDateOne = 0;
    sHudMaskFullClearStrips = sHudMaskFullClearSweeps = 0;
    Psp_Log("psp: top HUD sprite depth: total=%u bypassed=%u ZTE=%u Zwrite=%u mode=%d TFX=%u/%u/%u/%u CLAMP-S=%u/%u/%u/%u\n",
            sHudTopSpriteCount, sHudTopDepthSuppressed,
            sHudTopDepthRequested, sHudTopDepthWriteRequested,
            gPspHudTopOverlay,
            sHudTopTfxModes[0], sHudTopTfxModes[1],
            sHudTopTfxModes[2], sHudTopTfxModes[3],
            sHudTopClampModes[0], sHudTopClampModes[1],
            sHudTopClampModes[2], sHudTopClampModes[3]);
    sHudTopSpriteCount = sHudTopDepthSuppressed = 0;
    sHudTopDepthRequested = sHudTopDepthWriteRequested = 0;
    memset(sHudTopTfxModes, 0, sizeof(sHudTopTfxModes));
    memset(sHudTopClampModes, 0, sizeof(sHudTopClampModes));
    Psp_Log("psp: GS sprite colors: sprites=%u distinct-vertex-RGBA=%u, flat_sprite=%d\n",
            sDiagFlatSprites, sDiagUnequalSpriteColors, gPspFlatSprite);
    sDiagFlatSprites = sDiagUnequalSpriteColors = 0;
    /* Depth asked for vs depth applied. gs_depth=0 applies none of it, so
       a battle window with triangles >> 0 and ZTE on most of them is a
       scene drawn without the occlusion it was authored with. ZTST is
       NEVER/ALWAYS/GEQUAL/GREATER; z-range is the raw 24-bit GS Z of
       triangle vertices (min == max would mean depth cannot help). */
    Psp_Log("psp: GS depth: mode=%d triangles=%u (work=%u, ZTE=%u, Zwrite=%u, flat-Z=%u) ZTST=%u/%u/%u/%u z-range=%08x..%08x sprites=%u (ZTE=%u)\n",
            gPspGsDepth, sDiagZTri, sDiagZTriWork, sDiagZTriTest,
            sDiagZTriWrite, sDiagZTriFlat,
            sDiagZFunc[0], sDiagZFunc[1], sDiagZFunc[2], sDiagZFunc[3],
            sDiagZTri ? sDiagZMin : 0, sDiagZMax,
            sDiagZSprite, sDiagZSpriteTest);
    sDiagZTri = sDiagZTriTest = sDiagZTriWrite = sDiagZTriWork = 0;
    sDiagZTriFlat = sDiagZSprite = sDiagZSpriteTest = 0;
    memset(sDiagZFunc, 0, sizeof(sDiagZFunc));
    sDiagZMin = 0xFFFFFFFFu;
    sDiagZMax = 0;
    Psp_Log("psp: GS fast CLUT hashing: batched=%u checked=%u mismatches=%u enabled=%d\n",
            sClutFastCalls, sClutSlowSamples, sClutHashMismatches, sClutRectSafe);
    sClutFastCalls = sClutSlowSamples = sClutHashMismatches = 0;
    Psp_Log("psp: GS white sprite modulation: baked-draws=%u cache-hits=%u created=%u enabled=%d\n",
            sDiagWhiteSpriteDraws, sDiagWhiteTextureHits,
            sDiagWhiteTextureBuilds, gPspWhiteModulate);
    sDiagWhiteSpriteDraws = sDiagWhiteTextureHits = sDiagWhiteTextureBuilds = 0;
    Psp_Log("psp: GS sprite DECAL: RGBA=%u RGB=%u (TEX0 TFX=1; GU_REPLACE)\n",
            sDiagSpriteDecalRgba, sDiagSpriteDecalRgb);
    sDiagSpriteDecalRgba = sDiagSpriteDecalRgb = 0;
    Psp_Log("psp: GS sprite FIX: draws=%u full-coverage=%u (FIX>=0x80) converted=%d special-TFX=%u\n",
            sDiagSpriteFixUsed, sDiagSpriteFixOpaque,
            gPspGsFixScale, sDiagSpriteHighlight);
    sDiagSpriteFixUsed = sDiagSpriteFixOpaque = sDiagSpriteHighlight = 0;
    Psp_Log("psp: GS sprite TEX: CLAMP-S=%u/%u/%u/%u CLAMP-T=%u/%u/%u/%u TFX=%u/%u/%u/%u TCC_RGB=%u ABE=%u\n",
            sDiagClampS[0], sDiagClampS[1], sDiagClampS[2], sDiagClampS[3],
            sDiagClampT[0], sDiagClampT[1], sDiagClampT[2], sDiagClampT[3],
            sDiagTfx[0], sDiagTfx[1], sDiagTfx[2], sDiagTfx[3],
            sDiagTccRgb, sDiagBlendOn);
    memset(sDiagClampS, 0, sizeof(sDiagClampS));
    memset(sDiagClampT, 0, sizeof(sDiagClampT));
    memset(sDiagTfx, 0, sizeof(sDiagTfx));
    sDiagTccRgb = sDiagBlendOn = 0;
    Psp_Log("psp: GS pending features: ALPHA.C=FIX=%u (zero=%u low=%u mid=%u high=%u), "
            "CLD0/CBP0 last-CLUT candidates=%u, ZTEST requested=%u, ZWRITE requested=%u\n",
            sDiagFixUsed, sDiagFixRange[0], sDiagFixRange[1],
            sDiagFixRange[2], sDiagFixRange[3], sDiagClutReuseCandidate,
            sDiagZTestRequested, sDiagZWriteRequested);
    sDiagFixUsed = sDiagClutReuseCandidate = sDiagZTestRequested = sDiagZWriteRequested = 0;
    memset(sDiagFixRange, 0, sizeof(sDiagFixRange));
    sDiagDrawMain = sDiagDrawWork = sDiagSkip16 = sDiagSkipDepth = 0;
    sDiagSkipPsm = sDiagSkipTexMem = sDiagSkipNoTarget = 0;
    sDiagAlphaKeep = sDiagAlphaOther = sDiagAlphaRefNonzero = 0;
    memset(sDiagAlphaOtherMode, 0, sizeof(sDiagAlphaOtherMode));
    Psp_Log("psp: GS scene paired-screen: on=%d main=%d other=%d routed=%u frames=%u screen-draws=%u work-draws=%u\n",
            gPspScenePairScreen, gGsMainFbp, gGsDisplayOtherFbp,
            sPairedDisplayRouted, sPairedDisplayFrames,
            sPairedScreenDraws, sPairedWorkDraws);
    sPairedDisplayRouted = sPairedDisplayFrames = 0;
    sPairedScreenDraws = sPairedWorkDraws = 0;
    Psp_Log("psp: GU swap verification: mismatch=%u unrecognized-return=%u swap_from_sdk=%d\n",
            sGuSwapMismatch, sGuSwapUnknown, gPspSwapFromSdk);
    sGuSwapMismatch = sGuSwapUnknown = 0;
    Psp_Log("psp: GU state batching: %u batches, %u changed states, %u draw-target switches\n",
            sGuStateCalls, sGuStateChanges, sGuTargetChanges);
    sGuStateCalls = sGuStateChanges = sGuTargetChanges = 0;
    /* Sum only completed samples from this ~2-second window.
       These values partition work, not all of it; chain includes submit,
       GPU sync, vsync, and the texture work listed as subcomponents. */
    Psp_Log("psp: PROFILE interval: gameCPU=%u ms, GSchain=%u ms (%u chains), GUfinishSync=%u ms, VBlankWait=%u ms, frameEnds=%u\n",
            (unsigned)(sProfGameNs / 1000000ull),
            (unsigned)(sProfChainNs / 1000000ull),
            sProfChainCount, (unsigned)(sProfGeSyncUs / 1000ull),
            (unsigned)(sProfVblankUs / 1000ull), sProfFrameEnds);
    Psp_Log("psp: PROFILE textures: key+CLUT+lookup ~= %u ms (%u/%u sampled, extrapolated x32); decode+swizzle+alloc=%u ms (%u successful decodes)\n",
            (unsigned)(sProfKeySampleUs * 32ull / 1000ull),
            sProfKeySamples, sProfKeyCalls, (unsigned)(sProfDecodeUs / 1000ull),
            sProfDecodeCount);
    sProfGameNs = sProfChainNs = 0;
    sProfKeySampleUs = sProfDecodeUs = 0;
    sProfGeSyncUs = sProfVblankUs = 0;
    sProfChainCount = sProfKeyCalls = sProfKeySamples = 0;
    sProfDecodeCount = sProfFrameEnds = 0;
    Psp_Log("psp: PROFILE GS draws ~= %u ms (%u calls, %u sampled x16)\n",
            (unsigned)(sProfDrawSampleUs * 16ull / 1000ull),
            sProfDrawCalls, sProfDrawSamples);
    sProfDrawCalls = sProfDrawSamples = 0;
    sProfDrawSampleUs = 0;
    Psp_Log("psp: texture cache: entries=%d/%d vram=%u KB ram=%u KB peak=%u KB hits=%u misses=%u evicted=%u alloc-failed=%u oversized=%u\n",
            sTexCount, MAX_TEX, sTexMem / 1024u, sTexRamMem / 1024u,
            sTexRamPeak / 1024u, sTexCacheHits, sTexCacheMisses,
            sTexCacheEvicted, sTexHeapFailed, sTexOversized);
    Psp_Log("psp: texture guarded reuse: hits=%u full-key=%u epoch=%u serial=%u\n",
            sTexReuseHits, sTexReuseFullKeys, gGsPspTextureEpoch, sTexSerial);
    sTexReuseHits = sTexReuseFullKeys = 0;
    Psp_Log("psp: texture LOD: tex_limit=%d reduced-new=%u cache-hits=%u cache-pixels-saved=%uK\n",
            gPspTexLimit, sLodReduced, sLodCacheHits,
            (unsigned)(sLodPixelsSaved / 1024ull));
    sLodReduced = sLodCacheHits = 0;
    sLodPixelsSaved = 0;
    /* PSP-specific summary from the ported Vita VIF unpack fast path.
       If few vectors use it, the next optimization should target the
       VU1 instruction interpreter rather than the VIF unpack loop. */
    {
        unsigned vuPerf[5];
        extern void GsVu1_PspPerf(unsigned out[5]);
        unsigned nativeSubword;
        extern unsigned GsVu1_PspNativeSubwordCount(void);
        GsVu1_PspPerf(vuPerf);
        nativeSubword = GsVu1_PspNativeSubwordCount();
        Psp_Log("psp: VIF/VU1: fast-unpack=%u vectors (native-8/16=%u), generic-unpack=%u vectors, "
                "VU1-interpreter=%u calls (%u sampled), estimated-VU1=%u ms\n",
                vuPerf[0], nativeSubword, vuPerf[1], vuPerf[2], vuPerf[3], vuPerf[4]);
    }
    {
        PspVuProfileBucket b[PSP_VU_BUCKET_COUNT];
        extern void GsVu1_PspBucketPerf(PspVuProfileBucket out[PSP_VU_BUCKET_COUNT]);
        GsVu1_PspBucketPerf(b);
        Psp_Log("psp: PROFILE VU1 programs: stage399=%u/%u/%u fighter127=%u/%u/%u fade124=%u/%u/%u effect451=%u/%u/%u\n",
                b[PSP_VU_BUCKET_STAGE].runs, b[PSP_VU_BUCKET_STAGE].instructions,
                b[PSP_VU_BUCKET_STAGE].estimated_ms,
                b[PSP_VU_BUCKET_FIGHTER].runs, b[PSP_VU_BUCKET_FIGHTER].instructions,
                b[PSP_VU_BUCKET_FIGHTER].estimated_ms,
                b[PSP_VU_BUCKET_FADE].runs, b[PSP_VU_BUCKET_FADE].instructions,
                b[PSP_VU_BUCKET_FADE].estimated_ms,
                b[PSP_VU_BUCKET_EFFECT].runs, b[PSP_VU_BUCKET_EFFECT].instructions,
                b[PSP_VU_BUCKET_EFFECT].estimated_ms);
        Psp_Log("psp: PROFILE VU1 other: shadow99/101=%u/%u/%u misc94/104=%u/%u/%u other=%u/%u/%u (values=runs/instructions/sampled-ms x16)\n",
                b[PSP_VU_BUCKET_SHADOW].runs, b[PSP_VU_BUCKET_SHADOW].instructions,
                b[PSP_VU_BUCKET_SHADOW].estimated_ms,
                b[PSP_VU_BUCKET_MISC_HLE].runs, b[PSP_VU_BUCKET_MISC_HLE].instructions,
                b[PSP_VU_BUCKET_MISC_HLE].estimated_ms,
                b[PSP_VU_BUCKET_OTHER].runs, b[PSP_VU_BUCKET_OTHER].instructions,
                b[PSP_VU_BUCKET_OTHER].estimated_ms);
    }
    Psp_Log("psp: texture pressure: evictions=%u recovered-RAM=%u scratch-failures=%u scratch-recovered=%u heap-failed=%u\n",
            sTexPressureEvictions, sTexRecoveredAllocations,
            sTexScratchFailures, sTexScratchRecovered, sTexHeapFailed);
    Psp_Log("psp: texture lifetime: old-frame-no-GE-wait=%u current-frame-GE-fence=%u fence-time=%u ms scratch-rgba=%u scratch-idx=%u\n",
            sTexEvictAfterFrame, sTexEvictSameFrame,
            (unsigned)(sTexEvictFenceUs / 1000ull),
            sScratchRgbaUses, sScratchIndicesUses);
    sTexEvictAfterFrame = sTexEvictSameFrame = 0;
    sTexEvictFenceUs = 0;
    sScratchRgbaUses = sScratchIndicesUses = 0;
    Psp_Log("psp: fb-alias: valid=%u fallback=%u reupload=%u unrendered=%u self=%u scratch-owner-changes=%u\n",
            sFbAliasHit, sFbAliasFallback, sFbAliasReupload,
            sFbAliasUnrendered, sFbAliasSelf, sFbWorkOwnerChanges);
}
