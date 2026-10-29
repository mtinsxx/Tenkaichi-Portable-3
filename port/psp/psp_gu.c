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
#include "psp_shadow6.h"
#include "psp_stage_clip_cache.h"
#include "psp_log_all.h"
#include "psp_gu_swap_check.h"
#include "psp_scene_pair.h"
#include "psp_hud_overlay.h"
#include "psp_vu_profile.h"
#include "psp_hud_mask.h"
#include "psp_fighter_vu1.h"
#include "psp_gs_fb16.h"
#include "psp_gs_depth_z.h"
#include "psp_gs_scissor.h"
#include "psp_gs_static_skip.h"
#include "psp_gu_scissor.h"
#include "psp_gs_frame_mask.h"
#include "psp_gs_afail.h"
#include "psp_gs_fb_reconstruct.h"
#include "psp_fb_snapshot_recycle.h"
#include "psp_stage_compose.h"
#include "psp_tex_reuse.h"
#include "psp_tex_lru.h"
#include "psp_t8_native.h"
#include "psp_native_t8_pack.h"
#include "psp_native_t8_index_cache.h"
#include "psp_t8_palette_refresh.h"
#include "psp_native_gu_passes.h"
#include "psp_texture_read_verify.h"
#include "psp_fb_lookup_policy.h"
#include "psp_gpu_probe.h"
#include "psp_vram_address.h"
#include "psp_tex_lifetime.h"
#include "psp_vertex_arena.h"

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512            /* the frame buffers' stride: a power of two the engine wants */
#define FB_SIZE (BUF_W * SCR_H * 2)
#define DEPTH_SIZE (BUF_W * SCR_H * 2)

/* Where everything sits in the PSP's graphics memory (2 MB). */
/* All PSP CPU VRAM reads/writes use the uncached 0x44000000 mirror.
 * 0x04000000 is the cached mirror: reading it after GE sync can
 * return stale CPU cache lines; writes may later overwrite GE pixels.
 * This also fixes initial clears and VRAM texture uploads to GE. */
#define VRAM_BASE ((void *)(uintptr_t)PSP_VRAM_UNCACHED_BASE)
#define FB0_OFF 0
#define FB1_OFF FB_SIZE
#define DEPTH_OFF (2 * FB_SIZE)
/* The battle alternates GS FBPs 336 and 368. They MUST NOT overwrite
   each other before a subsequent frame-as-texture sample. Reserve a
   second real PSP 5551 framebuffer. PSP VRAM = 2 MiB; the trade-off is
   272 KiB less VRAM texture space, not smaller fighter meshes. */
#define WORK_OFF (DEPTH_OFF + DEPTH_SIZE)
#define WORK_SIZE FB_SIZE
#define WORK2_OFF (WORK_OFF + WORK_SIZE)
#define WORK2_SIZE FB_SIZE
#define TEX_OFF (WORK2_OFF + WORK2_SIZE)
#define TEX_SIZE (0x200000u - TEX_OFF)

/* Guard the PSP's physical 2 MiB VRAM map at compile time. The three
   screen/depth surfaces and the one work surface must not overlap the
   texture region, regardless of later tuning. */
#if FB1_OFF < (FB0_OFF + FB_SIZE) || DEPTH_OFF < (FB1_OFF + FB_SIZE) || WORK_OFF < (DEPTH_OFF + DEPTH_SIZE) || WORK2_OFF < (WORK_OFF + WORK_SIZE) || TEX_OFF < (WORK2_OFF + WORK2_SIZE)
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
/* Maximum 1.5 MiB main RAM reserved for loss-limited GS feedback
 * snapshots. The PSP homebrew process must leave memory for GS VRAM,
 * decoded textures, microprogram interpreter, movie and sound. */
#define MAX_FB_SNAPSHOTS 3u

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
#include "psp_target_lifetime.h"
#include "psp_work_pair.h"

typedef struct Tex {
    uint64_t tex0, texa;
    uint32_t gen;
    uint32_t sourceGen;      /* PSMT8 source-only page hash, excludes CLUT */
    void *ptr;               /* VRAM or aligned main RAM, readable by GE */
    void *heap_alloc;        /* malloc base when stored in main RAM */
    void *clut;              /* 256 RGBA8888 entries, NULL for expanded textures */
    unsigned w, h, bytes;
    int fmt;                 /* GU_PSM_5551 / GU_PSM_8888 */
    int swizzled;            /* whether this texture uses the PSP's tiled layout */
    int white_sprite;        /* independent white-modulation texture variant */
    unsigned last;           /* frame last used: required for GE fence */
    uint32_t useTicket;      /* exact intra-frame LRU order, not a GPU fence */
    uint32_t clutTicket;     /* forces PSP CLUT reload after palette-pointer reuse */
} Tex;

typedef struct Target {
    uint32_t fbp;
    uint32_t psm; /* GS FRAME.PSM which produced this 5551 pixel view */
    void *color, *depth;
    unsigned last;
    uint32_t gen;  /* last GS upload generation when this FBP was rendered */
    int rendered;  /* a GU draw has written to this particular backing buffer */
    int used;
    void *snapshot;      /* 512x512 RGB5551 main-RAM GE texture */
    void *snapshotRaw;   /* original allocation, reused on target eviction */
    PspGsFbVersion snapVersion;
    unsigned snapshotSampledFrame; /* GE may still be sampling this RAM */
} Target;

static unsigned int __attribute__((aligned(64))) sList[65536];
static unsigned char *sVram = (unsigned char *)VRAM_BASE;
static unsigned sTexMem;     /* bytes of TEX_SIZE given out */
static unsigned sTexRamMem, sTexRamPeak;
static uint32_t sTexUseTicket;
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
static void *sTexClut;      /* T8 CLUT attached to the last cache hit/miss */
static uint32_t sTexClutTicket, sClutTicketNext;
static unsigned sT8NativeNew, sT8NativeHits, sT8NativeSaved;
static unsigned sT8RefVerified, sT8RefMismatch, sT8RefRebuilt;
/* Fixed-size 4x64 KiB source-index cache: retains four independent
 * PSMT8 images without increasing the old 256 KiB RAM footprint.
 * The live CLUT is always decoded separately from cached indices. */
static PspNativeT8Slot sT8IndexSlots[PSP_NATIVE_T8_CACHE_SLOTS];
static uint8_t sT8IndexBuffer[PSP_NATIVE_T8_CACHE_SLOTS][PSP_NATIVE_T8_SLOT_BYTES];
static uint32_t sT8IndexClock;
static unsigned sT8IndexHits, sT8IndexMisses, sT8IndexStored;
static unsigned sT8IndexEvicted, sT8IndexOversize;
static unsigned sT8ClutRefreshed, sT8ClutRefreshBlocked;
static unsigned sNativeT8PackLoaded, sNativeT8PackMissing, sNativeT8PackRejected;
static unsigned sFbReadLookupPreserved;
static Tex sTex[MAX_TEX];
static int sTexCount;
static PspTexReuse sTexReuse[PSP_TEX_REUSE_SLOTS];
static uint32_t sTexSerial = 1u;
static unsigned sTexReuseHits, sTexReuseFullKeys;
static unsigned sTexReuseUnrelatedUploads, sTexReuseEraseRepairs;
static Target sTargets[MAX_TARGETS];
/* GE draw commands reference pointers into this frame-owned immutable arena. */
static Vtx *sVb, *sVbArena;
static void *sVbArenaRaw;
static unsigned sVbCapacity, sVbCursor, sVbPeak, sVbArenaFences;
static unsigned long long sVbArenaFenceUs;
static int sVbCount, sVbMode; /* count, and what primitives the batch holds */
static int sReady;
static unsigned sFrame;
static PspDiagAll sPspLogAll;
static unsigned sDraws, sSkipped, sNewTex, sBatches;
static unsigned sGsEmptyClipRejects, sGsPrimitiveClipRejects;
static unsigned sGsNativeStaticSkips;
static unsigned sGsFbMaskDraws,sGsFbMaskPartial,sGsFbMaskAlphaOnly;
static unsigned sLastDrawStaticSkip;
/* PSP-native stage (VU1 program 4), without replacing the game/GS logic. */
static PspStage4Vertex sStage4Prepared[1024];
static PspStageClipVertex sStage4ClipSource[1024];
static unsigned sStage4ClipFlag[1024];
static uint32_t sStage4PreparedCount;
static PspStageClipCache sStage4ClipCache;
static unsigned sNativeStageCachedClipTriangles;
static unsigned sNativeStageClipTriangles, sNativeStageClipOutside, sNativeStageClipFailed;
static unsigned sNativeStageBatches, sNativeStageVerts, sNativeStageFallback;
static unsigned sNativeStageProjectionRejects;
/* VU1 program6 had a no-op backend. GPU ground-shadow strips now
   use the exact screen/UV input layout and conservative clip guards. */
static PspShadow6Vertex sShadow6Prepared[1024];
static unsigned char sShadow6Valid[1024];
static unsigned sShadow6Calls,sShadow6Triangles,sShadow6Skipped;
/* Native program-4 triangle GS batching: the original GsGpu_Draw
   still handles the first triangle of each strip, subsequent triangles
   reuse its texture/target/state and emit identical GU vertices. */
static unsigned sNativeStageFastStarts, sNativeStageFastTriangles;
/* Same no-state-change optimization for the two fighter VU1 layers:
   each layer has one TEX0/PRIM; reuse the first triangle's proven state. */
static unsigned sNativeFighterFastStarts, sNativeFighterFastTriangles;
static unsigned sGuNativePass=PSP_GU_PASS_NONE;
static unsigned sGuNativePassBarriers,sGuNativePassStage,sGuNativePassFighter,sGuNativePassSprites;
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
static unsigned sPixelProbeShown, sPixelProbeLastFrame;
static unsigned sPixelProbeFrame, sPixelProbeSampleCount;
static PspGpuPixelProbe sPixelProbeScreen, sPixelProbeWork0, sPixelProbeWork1;
static unsigned sPixelProbeScene, sPixelProbeFighter;
static unsigned sTargetsRegistered;
/* Each physical PSP WORK surface has its own logical GS owner.
   GS 336 lives in WORK0, GS 368 in WORK1; other scratch targets
   conservatively share WORK0, with strict owner invalidation. */
static uint32_t sWorkOwnerFbp[PSP_WORK_SURFACES] = {UINT32_MAX,UINT32_MAX};
static unsigned sWorkPairSamples, sWorkPairOwnerMisses;
static unsigned sFbSnapshotsSaved, sFbSnapshotsSampled;
static unsigned sFbSnapshotsAllocFail, sFbSnapshotsDeferred;
static unsigned sFbSnapshotAllocs, sFbSnapshotBudgetSkip;
static unsigned sFbSnapshotRecycled;
static unsigned sFbAliasHit, sFbAliasFallback, sFbAliasReupload;
static unsigned sFbAliasUnrendered, sFbAliasSelf, sFbWorkOwnerChanges;
static unsigned sFbPsmAliasRejected, sGsAfailSecondPass, sGsAfailHudFallback;
static unsigned sGsUploadSnapshot, sGsUploadUnsupported;
static unsigned sStageCompositions, sStageComposeRejected, sStageTagged;
static unsigned sStageRejectOffscreen,sStageRejectSame,sStageRejectStale;
static unsigned sStageRejectOwner,sStageRejectUnready,sStageRejectAlready,sStageRejectOther;
static unsigned sTargetRebindInvalid, sTargetScreenCleared;
extern int gPspTargetFreshness;
extern int gGsMainFbp; /* gs_sony.c: the frame buffer the frame is drawn into */
extern int gGsDisplayOtherFbp; /* libgraph's paired SCREEN buffer */
extern int gPspScenePairScreen;
extern int gPspScenePairSeparate;
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
extern int gPspLogAll;
/* Immutable sample of live GS context, recorded once per incident class. */
static void Psp_DiagAllIncident(unsigned kind, int ctx, uint32_t detail) {
    PspDiagAllSample s;
    if (!gPspLogAll || kind >= PSP_DIAG_COUNT) return;
    /* Hot geometry and framebuffer queries can occur tens of thousands
       of times. Build the costly register snapshot only ONCE per kind
       and interval; subsequent events just update two small counters. */
    if (sPspLogAll.counts[kind]) {
        if (sPspLogAll.counts[kind] != UINT32_MAX) ++sPspLogAll.counts[kind];
        sPspLogAll.lastFrame[kind] = sFrame;
        return;
    }
    memset(&s, 0, sizeof(s));
    s.frame=sFrame; s.ctx=ctx; s.detail=detail;
    s.prim=gGs.prim;
    if (ctx>=0 && ctx<=1) {
        s.fbp=(uint32_t)(gGs.frame[ctx] & 0x1FFu);
        s.framePsm=(uint32_t)((gGs.frame[ctx]>>24)&63u);
        s.zbp=(uint32_t)(gGs.zbuf[ctx] & 0x1FFu);
        s.tex0=gGs.tex0[ctx];
        s.tbp=(uint32_t)((gGs.tex0[ctx] & 0x3FFFu)/32u);
        s.test=gGs.test[ctx];
    }
    PspDiagAll_Record(&sPspLogAll,kind,&s);
}
static void Psp_DiagAllReport(void) {
    unsigned i;
    if (!gPspLogAll) return;
    Psp_Log("psp: LOG_ALL v1 begin sample-end-frame=%u depth-mode=%d current-main-fbp=%d previous-fbp=%d draw-offset=0x%x\n",
            sFrame,gPspGsDepth,gGsMainFbp,gGsDisplayOtherFbp,sMainDrawOffset);
    for (i=0;i<PSP_DIAG_COUNT;i++) {
        const PspDiagAllSample *s=&sPspLogAll.first[i];
        if (!sPspLogAll.counts[i]) continue;
        Psp_Log("psp: LOG_ALL kind=%s count=%u first-frame=%u last-frame=%u sample-ctx=%d fbp=%u zbp=%u tbp=%u texpsm=%u fbpsm=%u test=%08llx prim=%llx detail=%u\n",
                PSP_DIAG_LABELS[i],sPspLogAll.counts[i],s->frame,
                sPspLogAll.lastFrame[i],s->ctx,s->fbp,s->zbp,s->tbp,
                (unsigned)((s->tex0>>20)&63u),
                s->framePsm,
                (unsigned long long)s->test,(unsigned long long)s->prim,s->detail);
    }
    Psp_Log("psp: LOG_ALL v1 end; non-listed categories had zero observed events. Counts do not prove visible pixel corruption.\n");
    PspDiagAll_Reset(&sPspLogAll);
}


/* ---- the state the engine is in (only what changed is sent again) ---- */
static struct {
    void *tex, *clut;
    uint32_t clutTicket;
    int tw, th, fmt, tmode;
    int tfx, tcc, wrapu, wrapv, filt;
    int blendop, src, dst, sfix, dfix, blend_on;
    int atest, afunc, aref, amask;
    int gsAfail, gsAtst; /* TEST.AFAIL and original PS2 ATST */
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
            (oldest < 0 ||
             PspTexLru_IsOlder(sTex[i].useTicket,sTex[oldest].useTicket))) {
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
        sVbCursor=0;
        sVb=sVbArena;
        sceGuStart(GU_DIRECT, sList);
        sAppliedValid = 0; /* fresh command list; force state setup */
        sTexEvictFenceUs += Psp_TimeUs() - begin;
        ++sTexEvictSameFrame;
    } else {
        ++sTexEvictAfterFrame;
    }
    free(sTex[oldest].heap_alloc);
    sTexRamMem -= sTex[oldest].bytes;
    /* Keep unrelated hot keys valid after descriptor array compaction;
       invalidate ONLY the removed index and shift indices after it. */
    sTexReuseEraseRepairs += Psp_TexReuseOnErase(sTexReuse,
                          PSP_TEX_REUSE_SLOTS,(unsigned)oldest,sTexSerial);
    memmove(&sTex[oldest], &sTex[oldest + 1],
            (size_t)(sTexCount - oldest - 1) * sizeof(Tex));
    --sTexCount;
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
            if (oldest < 0 ||
                PspTexLru_IsOlder(sTex[i].useTicket,sTex[oldest].useTicket))
                oldest = i;
        }
        if (oldest < 0) break;
        sTexReuseEraseRepairs += Psp_TexReuseOnErase(sTexReuse,
                          PSP_TEX_REUSE_SLOTS,(unsigned)oldest,sTexSerial);
        memmove(&sTex[oldest], &sTex[oldest + 1],
                (size_t)(sTexCount - oldest - 1) * sizeof(Tex));
        --sTexCount;
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
static int psp_is_work(const void *backing) {
    return backing == sVram + WORK_OFF ||
           backing == sVram + WORK2_OFF;
}
static int psp_work_slot(const void *backing) {
    return backing == sVram + WORK2_OFF ? 1 :
           backing == sVram + WORK_OFF ? 0 : -1;
}
static void *target_color(uint32_t fbp) {
    unsigned other = sMainDrawOffset == FB0_OFF ? FB1_OFF : FB0_OFF;
    unsigned off = Psp_GsScenePairBackingOffset(
        fbp, gGsMainFbp, gGsDisplayOtherFbp,
        gPspScenePairScreen, gPspScenePairSeparate,
        sMainDrawOffset, other, WORK_OFF);
    if (off == WORK_OFF && PspWork_Slot(fbp) == 1u)
        off = WORK2_OFF;
    return sVram + off;
}

/* Preserve a physical PSP backing before another PS2 FBP overwrites it.
 * We enqueue GE transfer commands in the SAME list as the draws, so the
 * GPU sees exact draw -> copy -> overwrite ordering. No synchronous
 * readback, CPU pixel loop, global render stall or new texture format.
 * The target's version prevents reuse after any later draw/GS upload.
 * At most one transfer to a given RAM texture each PSP frame: the next
 * transfer could clobber a texture still referenced by queued GE draws. */
static int snapshot_target(int slot) {
    Target *t;
    uintptr_t aligned;
    if(slot<0 || slot>=MAX_TARGETS)return 0;
    t=&sTargets[slot];
    if(!t->used||!t->rendered)return 0;
    if(t->gen!=gGsPageGen[t->fbp & 511u])return 0;
    if(!PspGsFb_CanSave(&t->snapVersion,sFrame,
                            t->snapshotSampledFrame)) {
        ++sFbSnapshotsDeferred;
        return 0;
    }
    if(!t->snapshot) {
        if(sFbSnapshotAllocs>=MAX_FB_SNAPSHOTS) {
            /* The 1.5 MiB snapshot pool is fixed. Retire ONLY old,
               invalid, GPU-synchronized images rather than failing
               when another logical FBP needs to save its pixels.
               Do not evict any image sampled or written this frame. */
            int donor=-1,k;
            for(k=0;k<MAX_TARGETS;k++) {
                Target *old=&sTargets[k];
                if(k==slot || !old->snapshot) continue;
                if(PspGsFb_CanRecycleSnapshot(1,old->used,
                      &old->snapVersion,old->gen,
                      gGsPageGen[old->fbp & 511u],
                      old->snapshotSampledFrame,sFrame)) {
                    donor=k;
                    break;
                }
            }
            if(donor>=0) {
                Target *old=&sTargets[donor];
                t->snapshotRaw=old->snapshotRaw;
                t->snapshot=old->snapshot;
                old->snapshotRaw=NULL;
                old->snapshot=NULL;
                /* Preserve the old logical write counter; only its
                   obsolete RAM image is invalidated. */
                old->snapVersion.valid=0u;
                old->snapVersion.saved=0u;
                old->snapVersion.sourceGen=0u;
                old->snapVersion.frame=0u;
                ++sFbSnapshotRecycled;
            } else {
                ++sFbSnapshotBudgetSkip;
                return 0;
            }
        } else {
            t->snapshotRaw=malloc(PSP_GS_FB_SNAPSHOT_BYTES+63u);
            if(!t->snapshotRaw) {++sFbSnapshotsAllocFail;return 0;}
            aligned=((uintptr_t)t->snapshotRaw+63u)&~(uintptr_t)63u;
            t->snapshot=(void *)aligned;
            ++sFbSnapshotAllocs;
        }
        /* Zero GE texture's unused padding after EACH pool rebind.
           Cached PSP main memory must be flushed before GPU writes. */
        memset(t->snapshot,0,PSP_GS_FB_SNAPSHOT_BYTES);
        sceKernelDcacheWritebackInvalidateRange(t->snapshot,
                                                  PSP_GS_FB_SNAPSHOT_BYTES);
    }
    flush(); /* submit the last triangle BEFORE the GE copy */
    sceGuCopyImage(GU_PSM_5551,0,0,PSP_GS_FB_COPY_W,
                   PSP_GS_FB_COPY_H,BUF_W,(void *)PspVram_GePointer(t->color),
                   0,0,BUF_W,t->snapshot);
    sceGuTexFlush(); /* later GE texture reads must observe this copy */
    PspGsFb_Saved(&t->snapVersion,sFrame,t->gen);
    ++sFbSnapshotsSaved;
    return 1;
}
/* Reference GS memory reader: verify the optimized PSMT8 swizzle against
 * exactly the same logical GS source before uploading the texture to GE. */
typedef struct { uint32_t tbp, tbw, psm; } PspT8Ref;
static uint32_t psp_t8_ref_sample(void *opaque, unsigned x, unsigned y) {
    const PspT8Ref *r=(const PspT8Ref *)opaque;
    return Gs_VramRead(r->tbp,r->tbw,r->psm,x,y);
}
static int target_get(uint32_t fbp, int create) {
    int i, slot = -1;

    for (i = 0; i < MAX_TARGETS; i++) {
        if (sTargets[i].used && sTargets[i].fbp == fbp) {
            /* A GS TEX0 source lookup must NOT move the physical backing.
             * The old lookup called target_color after a screen swap,
             * changing the backing of a source image without drawing it.
             * Preserve both pixel ownership and its valid snapshot. The
             * crash-associated PR #3 restore and screen owner changes
             * are intentionally NOT included here. */
            if (!PspFbLookup_CanChangeBacking(create)) {
                if (sTargets[i].color != target_color(fbp))
                    ++sFbReadLookupPreserved;
                sTargets[i].last=sFrame;
                return i;
            }
            void *nextBacking = target_color(fbp);
            if (sTargets[i].rendered &&
                PspGsFb_BackingChanged((uintptr_t)sTargets[i].color,
                                        (uintptr_t)nextBacking)) {
                /* Before reusing a logical FBP with a DIFFERENT physical
                   PSP backing, preserve the old completed GE image.
                   The previous code left rendered=1 by default because
                   target_freshness was disabled, silently treating a
                   newly selected drawbuffer as already initialized. */
                if(!PspGsFb_SampleValid(&sTargets[i].snapVersion,
                                          sTargets[i].gen))
                    snapshot_target(i);
                sTargets[i].rendered=0;
                ++sTargetRebindInvalid;
            }
            sTargets[i].last = sFrame;
            sTargets[i].color = nextBacking;
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
            if (sTargets[i].fbp != (uint32_t)gGsMainFbp &&
                sTargets[i].snapshotSampledFrame != sFrame &&
                (slot < 0 || sTargets[i].last < sTargets[slot].last)) {
                slot = i;
            }
        }
    }
    if (slot < 0) {
        return -1;
    }
    sTargets[slot].used = 1;
    PspGsFb_Reset(&sTargets[slot].snapVersion);
    /* Reusing a slot is safe only if its RAM texture is not in the
       current GE list (see the snapshotSampledFrame LRU exclusion). */
    sTargets[slot].snapshotSampledFrame=UINT32_MAX;
    sTargets[slot].fbp = fbp;
    sTargets[slot].psm = UINT32_MAX;
    sTargets[slot].gen = 0;
    sTargets[slot].rendered = 0;  /* never sample uninitialized backing VRAM */
    sTargets[slot].last = sFrame;
    sTargets[slot].color = target_color(fbp);
    sTargets[slot].depth = sVram + DEPTH_OFF;
    if (++sTargetsRegistered <= 8) {
        Psp_Log("psp: GS framebuffer #%u fbp=%u mapped %s\n",
                sTargetsRegistered, fbp,
                psp_is_work(sTargets[slot].color) ? "WORK" : "SCREEN");
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

/* Opt-in offline texture conversion: users extract their own assets
 * locally and put a version-checked pack under PSP/GAME/BT3/native.
 * We never try to open any per-texture file unless the enable marker
 * exists, so ordinary users never suffer storage I/O on cache misses.
 * The 64-sample scalar differential below still checks every loaded
 * pack against the CURRENT GS texture, preventing stale/palette
 * incompatibility from reaching the GPU. */
static int psp_native_t8_try_read(uint32_t tbp,uint32_t tbw,uint32_t gen,
              unsigned w,unsigned h,uint8_t *dst) {
    static int enabled=-1;
    char path[192];
    FILE *fp;
    PspNativeT8Header header;
    size_t bytes;
    if(enabled<0) {
        fp=fopen("ms0:/PSP/GAME/BT3/native/enable.txt","rb");
        enabled=fp!=NULL;
        if(fp) {fclose(fp);Psp_Log("psp: optional native PSMT8 pack reader enabled\n");}
    }
    if(!enabled || !dst || !w || !h || w>512u || h>512u) return 0;
    snprintf(path,sizeof(path),
             "ms0:/PSP/GAME/BT3/native/t8_%04x_%08x_%ux%u.tp8",
             tbp,gen,w,h);
    fp=fopen(path,"rb");
    if(!fp) {
        if(sNativeT8PackMissing<4u)
            Psp_Log("psp: native T8 pack candidate tbp=%u tbw=%u gen=%08x size=%ux%u (missing)\n",
                    tbp,tbw,gen,w,h);
        ++sNativeT8PackMissing;
        return 0;
    }
    bytes=(size_t)w*h;
    if(fread(&header,1,sizeof(header),fp)!=sizeof(header) ||
       !PspNativeT8_Match(&header,tbp,tbw,gen,w,h) ||
       fread(dst,1,bytes,fp)!=bytes ||
       PspNativeT8_Fnv(dst,bytes)!=header.dataFnv ||
       fgetc(fp)!=EOF) {
        ++sNativeT8PackRejected;
        fclose(fp);
        return 0;
    }
    fclose(fp);
    ++sNativeT8PackLoaded;
    return 1;
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
    uint32_t gen = 0, indexGen = 0;
    Tex *t;
    uint32_t *px;
    int fmt;
    int paletteRefreshCandidate=-1;
    /* Sampling the lookup one time in 32 avoids adding a clock syscall
       to every cache hit during the game's thousands of GS draws. */
    int sampleKey = ((++sProfKeyCalls & 31u) == 0);
    unsigned long long keyStartUs = sampleKey ? Psp_TimeUs() : 0;
    unsigned reuseSlot, pageBase, pageCount;
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
    /* Compute only the page-span, not pixel hashes. A GS upload to an
       unrelated page must not force thousands of unchanged RGB stage
       textures through page hashing and the full cache scan again. */
    pageBase = tbp / 32u;
    pageCount = ((tbw ? tbw : 1u) * 64u * readH *
                 (bits == 24u ? 32u : bits) / 8u + 8191u) / 8192u + 1u;
    {
        int hot = Psp_TexReuseMatches(&sTexReuse[reuseSlot], t0, texa,
                                      gGsPspTextureEpoch, sTexSerial,
                                      lodCap, white_sprite, (unsigned)sTexCount);
        if (!hot && bits > 8u &&
            Psp_TexReuseKeyStable(&sTexReuse[reuseSlot], t0, texa,
                                  sTexSerial, lodCap, white_sprite,
                                  (unsigned)sTexCount) &&
            Psp_TexReusePagesStillMatch(&sTexReuse[reuseSlot], gGsPageGen,
                                         pageBase, pageCount)) {
            sTexReuse[reuseSlot].epoch = gGsPspTextureEpoch;
            ++sTexReuseUnrelatedUploads;
            hot = 1;
        }
        if (hot) {
        /* Source GS VRAM/CLUT and texture table unchanged since last key. */
        t = &sTex[sTexReuse[reuseSlot].index];
        ++sTexReuseHits;
        ++sTexCacheHits;
        if (white_sprite) ++sDiagWhiteTextureHits;
        if (tw != readW || th != readH) ++sLodCacheHits;
        t->last = sFrame;
        t->useTicket = ++sTexUseTicket;
        *tw_out = (int)t->w;
        *th_out = (int)t->h;
        *fmt_out = t->fmt;
        sTexSwizzled = t->swizzled;
        sTexClut = t->clut;
        sTexClutTicket = t->clutTicket;
        if (t->clut) ++sT8NativeHits;
        if (sampleKey) {
            sProfKeySampleUs += Psp_TimeUs() - keyStartUs;
            ++sProfKeySamples;
        }
        return t->ptr;
        }
    }
    ++sTexReuseFullKeys;
    pages = ((tbw ? tbw : 1) * 64 * readH * (bits == 24 ? 32 : bits) / 8 + 8191) / 8192;
    for (i = 0; i <= pages; i++) {
        gen = (gen ^ Gs_PageHash(tbp / 32 + i)) * 16777619u + i;
    }
    /* The PSMT8 indices can be retained when the source pages have
     * identical content, even if the CLUT changes. Do not use this for
     * PSMT8H, 4-bit textures or white-font baked variants. */
    indexGen = gen;
    if (bits <= 8) {
        gen = (gen ^ clut_hash(cbp, cpsm, bits == 8 ? 256 : 16)) * 16777619u;
    }
    for (i = 0; i < sTexCount; i++) {
        /* When the PS2 updates only the palette, a completed-frame
         * native GU_T8 texture may safely keep its swizzled index
         * allocation and only refresh its CLUT. Do not modify a
         * texture still referenced by the current GE command list. */
        if(psm==0x13u && !white_sprite && sTex[i].fmt==GU_PSM_T8 &&
           sTex[i].clut!=NULL && sTex[i].tex0==t0 &&
           sTex[i].texa==texa && sTex[i].sourceGen==indexGen &&
           sTex[i].w==tw && sTex[i].h==th &&
           sTex[i].gen!=gen) {
            PspT8PaletteIdentity identity={
                sTex[i].tex0,sTex[i].texa,sTex[i].sourceGen,
                sTex[i].w,sTex[i].h,sTex[i].last,1,sTex[i].white_sprite
            };
            if(PspT8Palette_CanRefresh(&identity,t0,texa,indexGen,
                                      tw,th,sFrame))
                paletteRefreshCandidate=(int)i;
            else
                ++sT8ClutRefreshBlocked;
        }
        if (sTex[i].tex0 == t0 && sTex[i].texa == texa && sTex[i].gen == gen &&
            sTex[i].w == tw && sTex[i].h == th &&
            sTex[i].white_sprite == white_sprite) {
            if (white_sprite) ++sDiagWhiteTextureHits;
            if (tw != readW || th != readH) ++sLodCacheHits;
            ++sTexCacheHits;
            sTex[i].last = sFrame;
            sTex[i].useTicket = ++sTexUseTicket;
            Psp_TexReuseRemember(&sTexReuse[reuseSlot], t0, texa,
                                 gGsPspTextureEpoch, sTexSerial,
                                 lodCap, white_sprite, (unsigned)i);
            if (bits > 8u)
                Psp_TexReuseRememberPages(&sTexReuse[reuseSlot], gGsPageGen,
                                           pageBase, pageCount);
            *tw_out = (int)sTex[i].w;
            *th_out = (int)sTex[i].h;
            *fmt_out = sTex[i].fmt;
            sTexSwizzled = sTex[i].swizzled;
            sTexClut = sTex[i].clut;
            sTexClutTicket = sTex[i].clutTicket;
            if (sTexClut) ++sT8NativeHits;
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
    /* Palette-only GPU resource update on a completed frame: no new
     * texture memory, no GS PSMT8 index decoding and no swizzle.
     * Scene draw ordering is unchanged. Original VIF/CLUT semantics
     * still come from GS memory and TEXA. */
    if(paletteRefreshCandidate>=0) {
        unsigned k;
        Tex *reused=&sTex[paletteRefreshCandidate];
        uint32_t *palette=(uint32_t *)reused->clut;
        for(k=0;k<256u;k++) {
            unsigned e=PspT8_Csm1Index(k);
            uint32_t c=Gs_VramRead(cbp,1,cpsm,e&15u,e>>4);
            palette[k]=PspT8_GuPaletteColor(Gs_Expand(c,cpsm));
        }
        if(reused->heap_alloc)
            sceKernelDcacheWritebackRange(palette,1024u);
        sceGuTexFlush();
        reused->gen=gen;
        reused->last=sFrame;
        reused->useTicket=++sTexUseTicket;
        reused->clutTicket=++sClutTicketNext;
        Psp_TexReuseRemember(&sTexReuse[reuseSlot],t0,texa,
                   gGsPspTextureEpoch,sTexSerial,lodCap,
                   white_sprite,(unsigned)paletteRefreshCandidate);
        *tw_out=(int)reused->w;
        *th_out=(int)reused->h;
        *fmt_out=reused->fmt;
        sTexSwizzled=reused->swizzled;
        sTexClut=palette;
        sTexClutTicket=reused->clutTicket;
        ++sT8ClutRefreshed;
        ++sTexCacheHits;
        return reused->ptr;
    }
    ++sTexCacheMisses;
    decodeStartUs = Psp_TimeUs();
    /* Fast path for the 8-bit PS2 stage textures, verified on the user's
       LOG_ALL as TEX0.PSM=19 (PSMT8). Previously every indexed texel
       expanded to RGBA32 then copied to RGBA5551/8888 on EVERY cache miss.
       Keep compressed 8-bit texels in GE-native GU_PSM_T8 and use its
       256-entry RGBA8888 CLUT. Same GS swizzle and alpha, 2-4x less
       texture memory, fewer repeated decodes and cache evictions.
       White-font variants from fe93677 remain RGBA baked, untouched.
       Do NOT apply this to PSMT8H (0x1b): different GS storage plane. */
    if (psm == 0x13u && !white_sprite) {
        uint8_t *idx;
        void *ptr=NULL,*heap_alloc=NULL;
        uint32_t *palette;
        unsigned indexBytes = PspT8_AlignedBytes(tw,th);
        unsigned allocated = PspT8_AllocatedBytes(tw,th);
        int tiled = tw >= 16u && th >= 8u;
        if (sScratchIndices && Psp_TexUseScratch(
                readW * readH, PSP_TEX_SCRATCH_INDEX_BYTES)) {
            idx=sScratchIndices; ++sScratchIndicesUses;
        } else {
            idx=tex_malloc_pressure((size_t)readW*readH);
        }
        if (!idx) return NULL;
        /* Keep four independent texture-index sources resident.
         * The input source-hash is taken before the palette hash:
         * a CLUT-only update leaves the indices reusable. The
         * format is only PSMT8, never PSMT8H or indexed depth. */
        {
        int cachedSlot=PspNativeT8Cache_Lookup(sT8IndexSlots,
                     PSP_NATIVE_T8_CACHE_SLOTS,tbp,tbw,indexGen,
                     readW,readH,++sT8IndexClock);
        if(cachedSlot>=0) {
            memcpy(idx,sT8IndexBuffer[cachedSlot],(size_t)readW*readH);
            ++sT8IndexHits;
        } else {
            ++sT8IndexMisses;
            if (!psp_native_t8_try_read(tbp,tbw,gen,readW,readH,idx) &&
                !Gs_VramReadIdx8(tbp,tbw,psm,readW,readH,idx,readW)) {
                tex_temp_free(idx);
                return NULL;
            }
            /* Verify indices against the scalar GS reference before
             * storing them. Cached data is checked by a full source
             * page hash on each later reuse, never just a frame tick. */
            {
                PspT8Ref ref={tbp,tbw,psm};
                unsigned badX=0u,badY=0u;
                ++sT8RefVerified;
                if(!PspT8_CompareSamples(idx,readW,readH,readW,
                        psp_t8_ref_sample,&ref,&badX,&badY)) {
                    unsigned x,y;
                    ++sT8RefMismatch;
                    if(sT8RefMismatch<=4u)
                        Psp_Log("psp: PSMT8 reference mismatch tbp=%u tbw=%u cbp=%u size=%ux%u pixel=%u,%u; rebuilding\n",
                            tbp,tbw,cbp,readW,readH,badX,badY);
                    for(y=0;y<readH;y++) for(x=0;x<readW;x++)
                        idx[y*readW+x]=(uint8_t)Gs_VramRead(tbp,tbw,psm,x,y);
                    ++sT8RefRebuilt;
                }
            }
            {
                int slot=PspNativeT8Cache_Choose(sT8IndexSlots,
                         PSP_NATIVE_T8_CACHE_SLOTS,readW,readH,
                         sT8IndexClock);
                if(slot>=0) {
                    if(sT8IndexSlots[slot].source.valid)
                        ++sT8IndexEvicted;
                    memcpy(sT8IndexBuffer[slot],idx,(size_t)readW*readH);
                    PspNativeT8Cache_Commit(&sT8IndexSlots[slot],
                                   tbp,tbw,indexGen,readW,readH,
                                   sT8IndexClock);
                    ++sT8IndexStored;
                } else {
                    /* Large source textures are decoded via the
                     * original GS reader, never truncated to 64 KiB. */
                    ++sT8IndexOversize;
                }
            }
        }
        }
        if (readW != tw || readH != th)
            Psp_LodSubsample8(idx,readW,readH,tw,th);
        if (sTexCount >= MAX_TEX) tex_evict(0);
        if (allocated <= MAX_TEX_BYTES/2 &&
            ((allocated+63u)&~63u) <= MAX_TEX_BYTES-sTexMem)
            ptr=tex_alloc(allocated);
        if (!ptr) ptr=tex_alloc_ram(allocated,&heap_alloc);
        if (!ptr) { tex_temp_free(idx); return NULL; }
        palette=(uint32_t *)((uint8_t *)ptr+indexBytes);
        if (tiled) swizzle((uint8_t *)ptr,idx,tw,th,1);
        else memcpy(ptr,idx,tw*th);
        for (i=0;i<256u;i++) {
            unsigned e=PspT8_Csm1Index(i);
            uint32_t c=Gs_VramRead(cbp,1,cpsm,e&15u,e>>4);
            palette[i]=PspT8_GuPaletteColor(Gs_Expand(c,cpsm));
        }
        if (heap_alloc) sceKernelDcacheWritebackRange(ptr,allocated);
        sceGuTexFlush();
        t=&sTex[sTexCount++];
        Psp_TexReuseRemember(&sTexReuse[reuseSlot],t0,texa,
                 gGsPspTextureEpoch,sTexSerial,lodCap,
                 white_sprite,(unsigned)(sTexCount-1));
        t->tex0=t0; t->texa=texa; t->gen=gen;
        t->sourceGen=indexGen;
        t->ptr=ptr; t->heap_alloc=heap_alloc; t->clut=palette;
        t->w=tw; t->h=th; t->bytes=allocated;
        t->fmt=GU_PSM_T8; t->swizzled=tiled;
        t->white_sprite=0; t->last=sFrame;
        t->useTicket=++sTexUseTicket;
        t->clutTicket=++sClutTicketNext;
        sTexSwizzled=tiled; sTexClut=palette;
        sTexClutTicket=t->clutTicket;
        ++sNewTex; ++sT8NativeNew;
        sT8NativeSaved += (tw*th*2u > allocated) ?
                          tw*th*2u-allocated : 0u;
        if (tw != readW || th != readH) {
            ++sLodReduced;
            sLodPixelsSaved+=(unsigned long long)readW*readH -
                             (unsigned long long)tw*th;
        }
        *tw_out=(int)tw; *th_out=(int)th; *fmt_out=GU_PSM_T8;
        tex_temp_free(idx);
        sProfDecodeUs+=Psp_TimeUs()-decodeStartUs;
        ++sProfDecodeCount;
        return ptr;
    }
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
        /* Append does not shift descriptor indices; erasures repair only
           affected TEX0 hot keys. GS uploads still change the epoch. */
        Psp_TexReuseRemember(&sTexReuse[reuseSlot], t0, texa,
                             gGsPspTextureEpoch, sTexSerial,
                             lodCap, white_sprite, (unsigned)(sTexCount - 1));
        if (bits > 8u)
            Psp_TexReuseRememberPages(&sTexReuse[reuseSlot], gGsPageGen,
                                       pageBase, pageCount);
        t->tex0 = t0;
        t->texa = texa;
        t->gen = gen;
        t->ptr = ptr;
        t->heap_alloc = heap_alloc;
        t->clut = NULL;
        sTexClut = NULL;
        t->clutTicket = 0;
        sTexClutTicket = 0;
        t->w = tw;
        t->h = th;
        t->fmt = fmt;
        t->swizzled = doSwizzle;
        t->white_sprite = white_sprite;
        sTexSwizzled = doSwizzle;
        t->bytes = bytes;
        t->last = sFrame;
        t->useTicket = ++sTexUseTicket;
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
                        (gPspGsDepth == 2 && !psp_is_work(sSt.target));
        int depthPrev = gPspGsDepth == 1 ||
                        (gPspGsDepth == 2 && !psp_is_work(p->target));
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
        PspGuClip c=PspGu_FromGs(sSt.scx,sSt.scy,sSt.scw,sSt.sch);
        if(c.w && c.h) {
            /* PSPSDK expects WIDTH/HEIGHT, not right/bottom coordinates. */
            sceGuScissor(c.x,c.y,c.w,c.h);
        } else {
            /* Draw() rejects empty GS scissor before state_apply().
               Keep this GU command legal even with GU_DEBUG enabled. */
            sceGuScissor(0,0,1,1);
        }
        sceGuEnable(GU_SCISSOR_TEST);
    }

    if (sSt.tex != NULL) {
        if (first || p->tex == NULL || sSt.fmt != p->fmt ||
            sSt.tmode != p->tmode) {
            sceGuTexMode(sSt.fmt, 0, 0, sSt.tmode);
        }
        if (sSt.clut && (first || p->clut != sSt.clut ||
                         p->clutTicket != sSt.clutTicket)) {
            /* PSP CLUT loads eight colors per block: 32 blocks = 256.
               Palette and index pixels are in one cache-owned allocation.
               No palette is reused after its texture is evicted. */
            sceGuClutMode(GU_PSM_8888,0,255,0);
            sceGuClutLoad(32,PspVram_GePointer(sSt.clut));
        }
        /* A work framebuffer can be changed between two reads of the
           SAME address; therefore always invalidate on a FB texture bind,
           even when the other texture registers were unchanged. */
        if (sSt.tex == sVram + WORK_OFF ||
            sSt.tex == sVram + WORK2_OFF ||
            sSt.tex == sVram + FB0_OFF ||
            sSt.tex == sVram + FB1_OFF) {
            sceGuTexFlush();
        }
        if (first || p->tex != sSt.tex || p->tw != sSt.tw ||
            p->th != sSt.th || p->fmt != sSt.fmt ||
            p->tmode != sSt.tmode) {
            sceGuTexImage(0, sSt.tw, sSt.th, sSt.tw,
                          PspVram_GePointer(sSt.tex));
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
        Psp_Log("psp: movie: first frame sent to PSP GU (480x272)\n");
    }
}

/* ------------------------------------------------------------------ drawing */
static int gs_atst_to_gu(unsigned atst) {
    switch(atst&7u) {
    case 0: return GU_NEVER;
    case 1: return GU_ALWAYS;
    case 2: return GU_LESS;
    case 3: return GU_LEQUAL;
    case 4: return GU_EQUAL;
    case 5: return GU_GEQUAL;
    case 6: return GU_GREATER;
    default:return GU_NOTEQUAL;
    }
}
static void flush(void) {
    if (sVbCount == 0) return;
    state_apply();
    sceKernelDcacheWritebackRange(sVb, (size_t)sVbCount * sizeof(Vtx));
    sceGuDrawArray(sVbMode, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, sVbCount, NULL, sVb);
    /* GS AFAIL: after drawing alpha-passing pixels, replay only the
       alpha-failing subset. The GS reference still depth-tests these
       fragments, but changes which destination planes receive writes.
       Preserve fragment order within each batched primitive group.
       HUD stencil operations own the alpha bit and keep their existing
       translation; those unusual AFAIL cases retain the legacy path. */
    if (sSt.atest && sSt.gsAtst!=1 &&
        PspGs_AfailNeedsReplay(1u,(unsigned)sSt.gsAfail)) {
        __typeof__(sSt) original=sSt;
        sSt.afunc=gs_atst_to_gu(PspGs_InverseAtst((unsigned)sSt.gsAtst));
        sSt.pixelMask|=PspGs_AfailPassMask((unsigned)original.gsAfail);
        if(!PspGs_AfailWritesZ((unsigned)original.gsAfail))
            sSt.zwrite=0;
        state_apply();
        sceGuDrawArray(sVbMode, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, sVbCount, NULL, sVb);
        ++sGsAfailSecondPass;
        sSt=original; /* next state_apply restores the primary GE state */
    }
    /* DrawArray stores a pointer; the GPU may not have read it yet.
       Move to an untouched span rather than overwriting the last draw. */
    sVbCursor=PspGuVertexArena_Commit(sVbCursor,(unsigned)sVbCount);
    if(sVbCursor>sVbPeak) sVbPeak=sVbCursor;
    sVb=sVbArena+sVbCursor;
    sVbCount = 0;
    sBatches++;
}

/* Reuse arena memory only once the previous GE list has completed.
 * A GPU fence is necessary only if this frame exceeds the RAM budget. */
static void psp_gu_vb_reserve(unsigned incoming) {
    if (PspGuVertexArena_Fits(sVbCursor,(unsigned)sVbCount,
                             incoming,sVbCapacity))
        return;
    flush();
    if (PspGuVertexArena_Fits(sVbCursor,0u,incoming,
                             sVbCapacity))
        return;
    {
        unsigned long long begin=Psp_TimeUs();
        sceGuFinish();
        sceGuSync(0,0);
        sceGuStart(GU_DIRECT,sList);
        sAppliedValid=0;
        sVbCursor=0;
        sVb=sVbArena;
        ++sVbArenaFences;
        sVbArenaFenceUs+=Psp_TimeUs()-begin;
    }
}
/* All GPU primitives still preserve original GS submission order.
 * Separate GE batches only where native stage, actor or sprite
 * ownership changes. No arbitrary sorting across GS depth, alpha,
 * framebuffer snapshots or shader effects. */
static void psp_gu_mark_pass(unsigned pass) {
    if(PspGuPass_MustFlush(sGuNativePass,pass,(unsigned)sVbCount)) {
        flush();
        ++sGuNativePassBarriers;
    }
    sGuNativePass=PspGuPass_Next(pass);
    if(pass==PSP_GU_PASS_STAGE) ++sGuNativePassStage;
    else if(pass==PSP_GU_PASS_FIGHTER) ++sGuNativePassFighter;
    else if(pass==PSP_GU_PASS_SPRITE) ++sGuNativePassSprites;
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

    sLastDrawStaticSkip=PSP_GS_STATIC_NONE;
    if (!sReady) {
        if (profSample) {
            sProfDrawSampleUs += Psp_TimeUs() - profStart;
            ++sProfDrawSamples;
        }
        return;
    }
    sDraws++;
    if(type==6) psp_gu_mark_pass(PSP_GU_PASS_SPRITE);
    /* Reject GS pixels with no possible scissor coverage before doing
       expensive texture decode, framebuffer aliasing or GE state setup.
       This affects stage, fighter, menu and HUD alike. Do not reject
       nonfinite/ambiguous geometry; the legacy path handles that. */
    if (PspGs_ScissorViewportEmpty(sc)) {
        ++sGsEmptyClipRejects;
        ++sSkipped;
        return;
    }
    {
        float clipXY[6];
        for (i=0;i<n;i++) {
            clipXY[2*i]=v[i].x;
            clipXY[2*i+1]=v[i].y;
        }
        if (PspGs_ScissorReject(sc,clipXY,(unsigned)n)) {
            ++sGsPrimitiveClipRejects;
            ++sSkipped;
            return;
        }
    }
    /* Both GS PSMCT16 and PSMCT16S use the same pixel bit layout as
       GU_PSM_5551, so direct 5551 rasterization is safe if depth does
       not share the colour page. The different PS2 16S VRAM swizzle,
       framebuffer rereads and depth-as-texture are NOT implemented. */
    {
        uint32_t framePsm = (uint32_t)((frame >> 24) & 0x3Fu);
        int is16 = Gs_PsmBits(framePsm) == 16;
        int native16 = is16 &&
          Psp_GsCanRenderColor16(framePsm, (uint32_t)fbp,
                                 (uint32_t)zbp, gPspGsFb16);
        if (native16) ++sDiagFb16Native;
        if ((!native16 && is16) || fbp == zbp) {
            if (is16 && !native16) {
                sLastDrawStaticSkip=PSP_GS_STATIC_COLOR16;
                ++sDiagSkip16;
                Psp_DiagAllIncident(PSP_DIAG_DRAW_SKIP_COLOR16,ctx,framePsm);
            } else {
                sLastDrawStaticSkip=PSP_GS_STATIC_DEPTH_ALIAS;
                ++sDiagSkipDepth;
                Psp_DiagAllIncident(PSP_DIAG_DRAW_SKIP_DEPTH_ALIAS,ctx,(uint32_t)zbp);
            }
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
            sLastDrawStaticSkip=PSP_GS_STATIC_SPECIAL_TEXTURE;
            Psp_DiagAllIncident(PSP_DIAG_DRAW_SKIP_SPECIAL_TEXTURE,ctx,tpsm);
            sSkipped++;
            if (profSample) {
                sProfDrawSampleUs += Psp_TimeUs() - profStart;
                ++sProfDrawSamples;
            }
            return;
        }
        int src = target_get(tbp / 32, 0);
        int targetReady = 0;
        int snapshotReady = 0;
        if (src >= 0) {
            Target *old = &sTargets[src];
            if(old->rendered &&
               !PspVram_CanAliasGsPsm(old->psm,tpsm))
                ++sFbPsmAliasRejected;
            /* A GS memory page can hold a framebuffer at one time and an
               ordinary texture after a host-to-local upload. Do not bind
               stale PSP framebuffer VRAM when the GS upload generation has
               changed. This matches the reference GPU's alias check. */
            if ((tbp & 31u) == 0 && old->rendered &&
                old->gen == gGsPageGen[old->fbp & 511] &&
                PspVram_CanAliasGsPsm(old->psm,tpsm) &&
                old->fbp != (uint32_t)fbp &&
                /* Two PS2 display FBPs can share one PSP draw surface,
                   which must never be read as texture while being written. */
                old->color != target_color((uint32_t)fbp) &&
                (!psp_is_work(old->color) ||
                 PspWork_OwnerAllowsSample(
                      sWorkOwnerFbp[psp_work_slot(old->color)],old->fbp))) {
                if (psp_is_work(old->color)) ++sWorkPairSamples;
                /* Only mark this alias ready AFTER the correctly sized
                   snapshot has been produced. A failed allocation or
                   deferred copy must never bind NULL/stale snapshot
                   memory as a texture. */
                /* A completed native 5551 image can be sampled from a
                   padded main-RAM GE texture. Needed for full 272px
                   height; source PSP work images are only 512x272 and
                   cannot safely be GU 512x512 textures in-place.
                   Cap at one capture per logical FBP per PSP frame. */
                if(PspGsFb_SampleValid(&old->snapVersion,old->gen) ||
                   snapshot_target(src)) {
                    snapshotReady=1;
                    targetReady=1;
                    ++sFbAliasHit;
                    Psp_DiagAllIncident(PSP_DIAG_FB_ALIAS_VALID,ctx,tbp);
                } else {
                    /* Do not bind the physical 512x272 framebuffer as a
                       PSP 512x256 texture. That alias reads into another
                       VRAM allocation or truncates 16 rows, and it is
                       particularly unsafe when the destination overlaps.
                       Decode GS memory conservatively instead. */
                    ++sFbAliasFallback;
                    Psp_DiagAllIncident(PSP_DIAG_FB_OTHER_ALIAS,ctx,tbp);
                }
            } else if ((tbp & 31u) == 0 &&
                       old->gen==gGsPageGen[old->fbp & 511u] &&
                       PspVram_CanAliasGsPsm(old->psm,tpsm) &&
                       PspGsFb_SampleValid(&old->snapVersion,old->gen) &&
                       old->fbp!=(uint32_t)fbp) {
                /* The physical WORK surface was reused, but a previous
                   image survives as a GE-ready main RAM texture. */
                targetReady=1;
                snapshotReady=1;
                ++sFbSnapshotsSampled;
            } else {
                ++sFbAliasFallback;
                if (!PspVram_CanAliasGsPsm(old->psm,tpsm)) {
                    Psp_DiagAllIncident(PSP_DIAG_FB_INDEXED_FORMAT,ctx,tpsm);
                } else if (old->gen != gGsPageGen[old->fbp & 511]) {
                    ++sFbAliasReupload;
                    Psp_DiagAllIncident(PSP_DIAG_FB_INVALID_GENERATION,ctx,old->fbp);
                } else if (!old->rendered) {
                    ++sFbAliasUnrendered;
                    Psp_DiagAllIncident(PSP_DIAG_FB_NOT_RENDERED,ctx,old->fbp);
                } else if (old->fbp == (uint32_t)fbp) {
                    ++sFbAliasSelf;
                    Psp_DiagAllIncident(PSP_DIAG_FB_SELF_ALIAS,ctx,old->fbp);
                } else if (psp_is_work(old->color) &&
                           !PspWork_OwnerAllowsSample(
                                sWorkOwnerFbp[psp_work_slot(old->color)],old->fbp)) {
                    ++sWorkPairOwnerMisses;
                    Psp_DiagAllIncident(PSP_DIAG_FB_WORK_OWNER,ctx,old->fbp);
                } else {
                    Psp_DiagAllIncident(PSP_DIAG_FB_OTHER_ALIAS,ctx,old->fbp);
                }
            }
        }
        if (!targetReady) {
            tex = texture_get(ctx, type == 3 ? (unsigned)gPspTexLimit : 0u,
                              whiteSprite, &tw, &th, &fmt);
            if (tex == NULL) {
                ++sDiagSkipTexMem;
                Psp_DiagAllIncident(PSP_DIAG_DRAW_SKIP_TEX_DECODE,ctx,
                                    (uint32_t)((t0>>20)&63u));
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
            sTexClut = NULL;
            sTexClutTicket = 0;
            /* All GPU-produced framebuffer textures use an explicitly
               padded 512x512 snapshot. Never sample bare FB0/FB1/WORK
               VRAM as a 512x256 texture with an invalid height/pitch. */
            tex = sTargets[src].snapshot;
            sTargets[src].snapshotSampledFrame=sFrame;
            tw = 512;
            th = 512;
            fmt = GU_PSM_5551;
            /* In a framebuffer alias the source GS coordinates describe
               512x448 pixels; the PSP physical content is 480x272.
               These multipliers convert PS2 texel units to real PSP
               backing texels, rather than expanding coordinates by
               TEX0.TW / PSP_SCREEN which caused missing image edges. */
            us = PspGsFb_PhysicalU(1.f);
            vs = PspGsFb_PhysicalV(1.f);
        }
    }
    targetIndex = target_get((uint32_t)fbp, 1);
    if (targetIndex < 0) {
        ++sDiagSkipNoTarget;
        Psp_DiagAllIncident(PSP_DIAG_DRAW_SKIP_NO_TARGET,ctx,(uint32_t)fbp);
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
            !psp_is_work(sTargets[targetIndex].color),
            v[0].x, v[0].y, v[1].x, v[1].y);
    }
    if (gPspHudMaskFix && type == 6 &&
        !psp_is_work(sTargets[targetIndex].color) &&
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
        !psp_is_work(sTargets[targetIndex].color)) {
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
    if (psp_is_work(sTargets[targetIndex].color)) {
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
        if (psp_is_work(sTargets[targetIndex].color)) ++sDiagZTriWork;
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
    /* Invalidate only when a DIFFERENT logical GS FBP replaces the
       owner of the SAME physical PSP work buffer. The 336/368 pair
       remains alive concurrently on separate VRAM surfaces. */
    {
        int workSlot = psp_work_slot(sTargets[targetIndex].color);
        if (workSlot >= 0 && sWorkOwnerFbp[workSlot] != (uint32_t)fbp) {
            int k;
            flush();
            ++sFbWorkOwnerChanges;
            ++sFrameWorkOwners;
            Psp_DiagAllIncident(PSP_DIAG_FB_WORK_REPLACED,ctx,
                                sWorkOwnerFbp[workSlot]);
            for (k = 0; k < MAX_TARGETS; ++k)
                if (k != targetIndex && sTargets[k].used &&
                    sTargets[k].color == sTargets[targetIndex].color) {
                    /* Copy the actual completed GE pixels before
                       the next owner overwrites them. The copy stays
                       queued in submission order; no CPU synchronizing. */
                    snapshot_target(k);
                    sTargets[k].rendered = 0;
                }
            sWorkOwnerFbp[workSlot] = (uint32_t)fbp;
        }
    }
    /* Record that the image matches the current GS upload generation,
       so an upload over the same page makes subsequent sampling fall back
       to the actual decoded texture instead of stale framebuffer pixels. */
    sTargets[targetIndex].gen = gGsPageGen[(uint32_t)fbp & 511];
    sTargets[targetIndex].psm = (uint32_t)((frame >> 24) & 63u);
    sTargets[targetIndex].rendered = 1;
    PspGsFb_OnDraw(&sTargets[targetIndex].snapVersion);
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
        n.tex = tex; n.clut = tme && !texIsTarget ? sTexClut : NULL;
        n.clutTicket = n.clut ? sTexClutTicket : 0u;
        n.tw = tw; n.th = th; n.fmt = fmt;
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
        n.gsAtst=afunc;
        /* The first pass always respects TEST.ATE. Non-KEEP modes get
           a second pass for the fragment subset that failed the test. */
        n.gsAfail=(int)((test>>12)&3u);
        n.atest=(int)(test&1u);
        if(hudMaskMode!=PSP_HUD_MASK_NONE && n.gsAfail!=0) {
            /* The battle HUD's DATE/stencil emulation uses the sole GU
               alpha bit. Do not let a second pass erase that stencil. */
            n.gsAfail=0;
            n.atest=0;
            ++sGsAfailHudFallback;
        }
        n.afunc=gs_atst_to_gu((unsigned)afunc);
        n.aref = gPspAlphaFix
               ? (int)alpha_scale((unsigned)((test >> 4) & 0xFF))
               : (int)((test >> 4) & 0xFF);
        n.amask = gPspAlphaFix ? 0xFF : 0;
        /* GS FRAME.FBMSK applies to ALL draw types, not just battle HUD.
           Missing masks allowed alpha-only and masked RGB passes to
           overwrite colors in arenas, fighters and gauge layers.
           The HUD stencil translation below retains its exact mask. */
        n.pixelMask=PspGs_FrameMaskToGu(frame);
        if(n.pixelMask) {
            ++sGsFbMaskDraws;
            if(PspGs_FrameMaskPartial((uint32_t)(frame>>32)))
                ++sGsFbMaskPartial;
            if((frame>>32 & 0x00FFFFFFu)==0x00FFFFFFu)
                ++sGsFbMaskAlphaOnly;
        }
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
    psp_gu_vb_reserve((unsigned)n);
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
            /* Framebuffer-as-texture STQ has logical GS dimensions
               encoded in TEX0, NOT the 512x512 POT size used for the
               PSP main-RAM staging texture. Using th=512 unconditionally
               stretched textures whose original GS TEX0.TH=256, etc. */
            unsigned logicalW=texIsTarget ?
                (1u << ((t0 >> 26) & 15u)):(unsigned)(tw?tw:1);
            unsigned logicalH=texIsTarget ?
                (1u << ((t0 >> 30) & 15u)):(unsigned)(th?th:1);
            if(logicalW>1024u)logicalW=1024u;
            if(logicalH>1024u)logicalH=1024u;
            u=(s->s/q)*(float)logicalW;
            vv=(s->t/q)*(float)logicalH;
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

/* A stage and its 3D actors can target different logical GS FBPs.
   Remember only REAL accepted native stage rasterization, and transfer
   it to the fighter's PSP screen target *before* the fighter's first
   triangles. Keep the original GS target routing/snapshots intact. */
static struct {
    unsigned frame, written;
    uint32_t fbp, gen;
    uintptr_t backing;
    int valid;
} sStageImage;
static unsigned sStageComposeFrame = UINT32_MAX;
static uintptr_t sStageComposeDest;

static void Psp_StageRecordDraw(int ctx) {
    uint32_t fbp;
    unsigned i;
    if (ctx < 0 || ctx > 1 || !sSt.target) return;
    fbp = (uint32_t)(gGs.frame[ctx] & 0x1FFu);
    for (i=0u; i<MAX_TARGETS; ++i) {
        Target *t=&sTargets[i];
        if (t->used && t->fbp==fbp && t->rendered &&
            t->color==sSt.target && t->snapVersion.written) {
            sStageImage.frame=sFrame;
            sStageImage.written=t->snapVersion.written;
            sStageImage.fbp=fbp;
            sStageImage.gen=t->gen;
            sStageImage.backing=(uintptr_t)t->color;
            sStageImage.valid=1;
            ++sStageTagged;
            return;
        }
    }
}

static void Psp_StageComposeBeforeFighter(int ctx) {
    uintptr_t dst,src;
    unsigned i;
    int sourceReady=0,ownerValid=1,screenDest,already;
    if (!sStageImage.valid || ctx<0 || ctx>1) return;
    dst=(uintptr_t)target_color((uint32_t)(gGs.frame[ctx]&0x1FFu));
    src=sStageImage.backing;
    screenDest = dst==(uintptr_t)(sVram+FB0_OFF) ||
                 dst==(uintptr_t)(sVram+FB1_OFF);
    already=sStageComposeFrame==sFrame && sStageComposeDest==dst;
    /* The physical WORK slot can be reassigned to another logical GS
       target; never copy if the stage no longer owns that backing. */
    if(src==(uintptr_t)(sVram+WORK_OFF) ||
       src==(uintptr_t)(sVram+WORK2_OFF)) {
        int slot=psp_work_slot((void *)src);
        ownerValid = slot>=0 &&
                     sWorkOwnerFbp[slot]==sStageImage.fbp;
    }
    for(i=0u;i<MAX_TARGETS;i++) {
        const Target *t=&sTargets[i];
        if(t->used && t->fbp==sStageImage.fbp &&
           (uintptr_t)t->color==src && t->rendered &&
           t->gen==sStageImage.gen &&
           t->snapVersion.written==sStageImage.written) {
            sourceReady=1;
            break;
        }
    }
    if(!PspStageCompose_ShouldCopy(sFrame,sStageImage.frame,
        sourceReady ? sStageImage.written : 0u,sStageImage.written,
        src,dst,screenDest,sourceReady,ownerValid,already)) {
        ++sStageComposeRejected;
        if (!screenDest) ++sStageRejectOffscreen;
        else if (src==dst) ++sStageRejectSame;
        else if ((unsigned)(sFrame-sStageImage.frame)>1u) ++sStageRejectStale;
        else if (!ownerValid) ++sStageRejectOwner;
        else if (!sourceReady) ++sStageRejectUnready;
        else if (already) ++sStageRejectAlready;
        else ++sStageRejectOther;
        return;
    }
    /* One ordered GE copy of 480x272 5551 pixels, never a CPU loop.
       Flush pending GS triangles before the copy; subsequent fighter
       geometry remains on dst and appears in front of this background.
       No copying to a render target currently aliased to the source. */
    flush();
    sceGuCopyImage(GU_PSM_5551,0,0,SCR_W,SCR_H,BUF_W,
                   (void *)PspVram_GeAddress(src),
                   0,0,BUF_W,(void *)PspVram_GeAddress(dst));
    sceGuTexFlush();
    sStageComposeFrame=sFrame;
    sStageComposeDest=dst;
    ++sStageCompositions;
}

/* Reused by the stage and fighter native VU1 renderers. Defined after
   the fighter adapter to preserve the original GS->GU dispatch order. */
typedef struct PspStageFastBatch {
    int active;
    PspGsStripSkip stableSkip;
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
    /* Copy a recent stage from another physical buffer before the
       first native fighter strip, never after its geometry or HUD. */
    Psp_StageComposeBeforeFighter(ctx);
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
    PspStageClipCache_Reset(&sStage4ClipCache);
    ++sFrameStageInputs;
    if (!gPspNativeStage4 || count < 3 || count > 1024 ||
        !sReady || ctx < 0 || ctx > 1 || !vertices || !consts) {
        Psp_DiagAllIncident(PSP_DIAG_STAGE_BAD_ARGS,ctx,count);
        return 0;
    }
    ox = (float)(gGs.xyoffset[ctx] & 0xFFFFu) / 16.0f;
    oy = (float)((gGs.xyoffset[ctx] >> 32) & 0xFFFFu) / 16.0f;
    for (i = 0; i < count; ++i) {
        const float *p = vertices + i * 12u;
        PspStage4Vertex *o = &sStage4Prepared[i];
        PspStageClipVertex *clip = &sStage4ClipSource[i];
        if (!PspStage4_ClipInput(clip, p, p + 4, p + 8, consts + 16)) {
            ++sNativeStageFallback;
            ++sNativeStageClipFailed;
            Psp_DiagAllIncident(PSP_DIAG_STAGE_INVALID_CLIP_INPUT,ctx,i);
            return 0;
        }
        sStage4ClipFlag[i] = clip->mask;
        if (clip->mask) {
            /* The old native path rejected an entire strip if ONE
               vertex was outside the frustum. This safe path clips
               individual triangles instead of rerunning all VU1. */
            if (!gPspNativeStageClip) {
                ++sNativeStageFallback;
                Psp_DiagAllIncident(PSP_DIAG_STAGE_CLIP_DISABLED,ctx,i);
                return 0;
            }
            /* Clipped fans use consts+32 while normal strips use consts.
               Preflight both homogeneous projections BEFORE writing any
               GS state. The VU1 interpreter owns every rejected strip. */
            if (!PspStage4_ProjectionCoherent(p,consts,clip->clip,
                                               consts+32)) {
                ++sNativeStageFallback;
                ++sNativeStageProjectionRejects;
                Psp_DiagAllIncident(PSP_DIAG_STAGE_INVALID_PROJECTION,ctx,i);
                return 0;
            }
        } else if (!PspStage4_TransformClipReady(o, p, p + 4, p + 8,
                                                   consts, clip->clip,
                                                   ox, oy)) {
            ++sNativeStageFallback;
            ++sNativeStageClipFailed;
            Psp_DiagAllIncident(PSP_DIAG_STAGE_INVALID_PROJECTION,ctx,i);
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
            PspStageClipVertex input[3];
            input[0] = sStage4ClipSource[idx0];
            input[1] = sStage4ClipSource[idx1];
            input[2] = sStage4ClipSource[idx2];
            if (!PspStageClipCache_Store(&sStage4ClipCache, i, input,
                                         consts + 32, ox, oy)) {
                ++sNativeStageFallback;
                ++sNativeStageClipFailed;
                Psp_DiagAllIncident(PSP_DIAG_STAGE_POLY_FAILURE,ctx,i);
                return 0;
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
    if (Psp_Stage4PrepareDirect(ctx, vertices, count, consts)) {
        Psp_DiagAllIncident(PSP_DIAG_STAGE_NATIVE_ACCEPT,ctx,count);
        return 1;
    }
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
        Psp_DiagAllIncident(PSP_DIAG_STAGE_TRIVIAL_CULL,ctx,count);
        return 1;
    }
    Psp_DiagAllIncident(PSP_DIAG_STAGE_INTERPRETER,ctx,count);
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
    psp_gu_mark_pass(fighter ? PSP_GU_PASS_FIGHTER : PSP_GU_PASS_STAGE);
    if (!fighter) Psp_DiagAllIncident(PSP_DIAG_STAGE_TRIANGLE,ctx,0);
    if (PspGsStripSkip_Next(&b->stableSkip)) {
        /* Same immutable GS registers throughout the native VU batch.
           Only previous statically rejected formats are memoized, NOT
           scissor culls, decoded-texture misses or scene visibility. */
        ++sGsNativeStaticSkips;
        ++sDraws; ++sSkipped;
        if (b->stableSkip.kind == PSP_GS_STATIC_COLOR16) {
            ++sDiagSkip16;
            Psp_DiagAllIncident(PSP_DIAG_DRAW_SKIP_COLOR16,ctx,0);
        } else if (b->stableSkip.kind == PSP_GS_STATIC_DEPTH_ALIAS) {
            ++sDiagSkipDepth;
            Psp_DiagAllIncident(PSP_DIAG_DRAW_SKIP_DEPTH_ALIAS,ctx,0);
        } else {
            ++sDiagSkipPsm;
            Psp_DiagAllIncident(PSP_DIAG_DRAW_SKIP_SPECIAL_TEXTURE,ctx,0);
        }
        return;
    }
    if (b->active) {
        int i;
        if (sVbCount + 3 > MAX_QUEUE) flush();
        if (sVbCount && sVbMode != GU_TRIANGLES) flush();
        psp_gu_vb_reserve(3u);
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
        if (psp_is_work(sSt.target)) {
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
        if (!fighter && sSkipped==skipped && sDraws==draws+1u)
            Psp_StageRecordDraw(ctx);
        if (sSkipped != skipped &&
            PspGsStripSkip_MaySkip(sLastDrawStaticSkip)) {
            PspGsStripSkip_Set(&b->stableSkip,sLastDrawStaticSkip);
            return;
        }
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
            const PspStage4Vertex *output =
                &sStage4ClipCache.projected[sStage4ClipCache.begin[i]];
            int n = sStage4ClipCache.length[i], k;
            if (n == 0) {
                ++sNativeStageClipOutside;
                continue;
            }
            ++sNativeStageCachedClipTriangles;
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
void GsGpu_DrawVu6(int ctx, const float *vertices, uint32_t count,
                   const float *consts) {
    uint32_t i;
    GsVertex tri[3];
    float ox,oy;
    if (!sReady||!vertices||!consts||count<3u||count>1024u) return;
    /* Program6's HLE sends ctx=-1 to use GS.PRIM's current context. */
    if(ctx<0) ctx=(int)((gGs.prim>>9)&1u);
    if(ctx>1) return;
    ++sShadow6Calls;
    ox=(float)(gGs.xyoffset[ctx]&0xffffu)/16.0f;
    oy=(float)((gGs.xyoffset[ctx]>>32)&0xffffu)/16.0f;
    for(i=0;i<count;i++) {
        sShadow6Valid[i]=(unsigned char)PspShadow6_Transform(
             &sShadow6Prepared[i],vertices+(size_t)i*16u,consts,ox,oy);
    }
    for(i=0;i+2u<count;i++) {
        uint32_t ids[3]={(i&1u)?i+1u:i,
                         (i&1u)?i:i+1u,i+2u},j;
        unsigned char valid[3],flags[3];
        for(j=0;j<3u;j++) {
            valid[j]=sShadow6Valid[ids[j]];
            flags[j]=sShadow6Prepared[ids[j]].drop;
        }
        if(!PspShadow6_TriangleVisible(valid,flags)) {
            ++sShadow6Skipped; continue;
        }
        for(j=0;j<3u;j++) {
            const PspStage4Vertex *v=&sShadow6Prepared[ids[j]].p;
            memset(&tri[j],0,sizeof(tri[j]));
            tri[j].x=v->x;tri[j].y=v->y;tri[j].z=v->z;
            tri[j].s=v->s;tri[j].t=v->t;tri[j].q=v->q;
            tri[j].r=v->r;tri[j].g=v->g;
            tri[j].b=v->b;tri[j].a=v->a;
        }
        /* Normal GS state path preserves depth/alpha/tex/scissor; this
           is conservative until the shadow fast-batch is validated. */
        GsGpu_Draw(3,ctx,tri);
        ++sShadow6Triangles;
    }
}

/* Called BEFORE a CPU host-to-GS upload overwrites either of the two
   512x448 GS display pages (0 and 0x70). Preserve a previously rendered
   PSP screen if a matching logical FBP is already tracked. Use the
   EXISTING 1.5 MiB virtual-framebuffer snapshot pool and its GE fences;
   never allocate another dedicated capture, or read an unsynchronized
   framebuffer with the CPU. This only preserves RGB5551 pixels and is
   not a PSMCT32 writeback into the swizzled GS 4 MiB memory. */
void GsGpu_FbUpload(int second) {
    const uint32_t fbp = second ? 0x70u : 0u;
    unsigned i;
    if (!sReady) return;
    flush(); /* ensure preceding PSP draws are queued before the copy */
    for (i=0u;i<MAX_TARGETS;i++) {
        Target *t=&sTargets[i];
        if(!t->used || t->fbp!=fbp)continue;
        if(t->rendered && t->gen==gGsPageGen[fbp&511u] &&
           (PspGsFb_SampleValid(&t->snapVersion,t->gen) ||
            snapshot_target((int)i))) {
            ++sGsUploadSnapshot;
            return;
        }
        break;
    }
    /* GS upload itself still uses gs_core.c VRAM. If no coherent GE
       image exists, don't claim the framebuffer was reconstructed. */
    ++sGsUploadUnsupported;
    ++sDiagUnimplementedFbUpload;
    Psp_DiagAllIncident(PSP_DIAG_FB_UPLOAD_UNSUPPORTED,-1,(unsigned)second);
}

void GsGpu_Native(int effect) {
    /* Native post-processing is not implemented on the PSP. */
    ++sDiagUnimplementedNativeEffect;
    Psp_DiagAllIncident(PSP_DIAG_NATIVE_EFFECT_UNSUPPORTED,-1,(unsigned)effect);
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
    sVbCursor=0;
    sVb=sVbArena;
    /* Capture the physical outputs of GE, not just submitted triangles.
       Sampling waits for the existing sync above: NO extra GPU stall.
       Keep diagnostic logs bounded on very long battles. Never touch
       depth bits or GU state (previous depth=0 test broke fighters). */
    if (gPspLogAll && (sFrameStageInputs || sFrameFighterInputs) &&
        (sPixelProbeShown < 24u || sFrame - sPixelProbeLastFrame >= 30u)) {
        const volatile uint16_t *screen=(const volatile uint16_t *)
            (sVram+sMainDrawOffset);
        const volatile uint16_t *w0=(const volatile uint16_t *)(sVram+WORK_OFF);
        const volatile uint16_t *w1=(const volatile uint16_t *)(sVram+WORK2_OFF);
        PspGpuPixelProbe a=PspGpuProbe5551(screen,BUF_W,SCR_W,SCR_H);
        PspGpuPixelProbe b=PspGpuProbe5551(w0,BUF_W,SCR_W,SCR_H);
        PspGpuPixelProbe c=PspGpuProbe5551(w1,BUF_W,SCR_W,SCR_H);
        /* Keep the last real-pixel sample in a separate summary so it
           survives as part of the next periodic report, not only the
           renderer's per-frame logs. No additional GPU/VRAM readback. */
        sPixelProbeScreen=a; sPixelProbeWork0=b; sPixelProbeWork1=c;
        sPixelProbeFrame=sFrame;
        sPixelProbeScene=sFrameStageInputs;
        sPixelProbeFighter=sFrameFighterInputs;
        ++sPixelProbeSampleCount;
        Psp_Log("psp: LOG_ALL PIXELS post-GE frame=%u main-fbp=%d screen=%u/%u hash=%08x work0-owner=%u work0=%u/%u hash=%08x work1-owner=%u work1=%u/%u hash=%08x stage-input=%u fighter-input=%u\n",
                sFrame, gGsMainFbp, a.colored,a.pixels,a.hash,
                sWorkOwnerFbp[0],b.colored,b.pixels,b.hash,
                sWorkOwnerFbp[1],c.colored,c.pixels,c.hash,
                sFrameStageInputs,sFrameFighterInputs);
        sPixelProbeLastFrame=sFrame;
        ++sPixelProbeShown;
    }
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
    /* Screen backing is about to be cleared; WORK is not cleared. */
    if (gPspTargetFreshness) {
        int i;
        for (i = 0; i < MAX_TARGETS; ++i) {
            if (sTargets[i].used && sTargets[i].rendered &&
                !PspTarget_KeepAfterScreenClear(sTargets[i].rendered,
                                                 sTargets[i].color,
                                                 sVram + WORK_OFF) &&
                !PspTarget_KeepAfterScreenClear(sTargets[i].rendered,
                                                 sTargets[i].color,
                                                 sVram + WORK2_OFF)) {
                sTargets[i].rendered = 0;
                ++sTargetScreenCleared;
            }
        }
    }
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
    /* Do NOT impose a black colour clear on every PSP frame: the PS2
       explicitly submits its own per-frame clear sprite via
       sceGsSwapDBuff -> GIF -> GsGpu_Draw.  The unconditional GU clear
       erased persistent stage backgrounds whenever a game frame only
       submitted fighters/effects. Both PSP display buffers are already
       initialized to black ONCE during Psp_GuInit. Keep the depth clear
       as a separate safeguard for PSP's single shared Z buffer.
       The normal GS sprites still clear colour when requested. */
    sceGuClearDepth(0);
    sceGuClear(GU_DEPTH_BUFFER_BIT);
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
    memset(sVram + WORK2_OFF, 0, WORK2_SIZE);
    Psp_Log("psp: startup VRAM clean: both screen buffers and work target cleared (one-time)\n");
    Psp_Log("psp: GS-native screen colour lifetime: frame-start GU color clear disabled; Sony GIF clear sprites kept; depth clear kept\n");
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
    /* Homebrew PSP may have limited heap when game tables are resident.
       Degrade arena size before failing boot; a smaller arena simply
       requires more GPU fences, never unsafe reuse of queued vertices. */
    {
        unsigned capacity=PSP_GU_VERTEX_FRAME_VERTICES;
        while (capacity>=MAX_QUEUE) {
            sVbArenaRaw=malloc((size_t)capacity*sizeof(Vtx)+63u);
            if(sVbArenaRaw) { sVbCapacity=capacity; break; }
            capacity/=2u;
        }
    }
    if (!sVbArenaRaw) return 0;
    sVbArena=(Vtx *)(((uintptr_t)sVbArenaRaw+63u)&~(uintptr_t)63u);
    sVb=sVbArena;
    sVbCursor=0;
    Psp_Log("psp: GE vertex lifetime arena=%u KiB (per-batch immutable until GE sync)\n",
            (unsigned)(sVbCapacity*sizeof(Vtx)/1024u));
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
    Psp_Log("psp: native ground shadows VU1 program6: calls=%u triangles=%u invalid-or-flagged=%u\n",
            sShadow6Calls,sShadow6Triangles,sShadow6Skipped);
    sShadow6Calls=sShadow6Triangles=sShadow6Skipped=0;
    Psp_Log("psp: GE vertex arena: peak=%u/%u vertices, mid-list-syncs=%u, sync-ms=%u\n",
            sVbPeak,sVbCapacity,sVbArenaFences,
            (unsigned)(sVbArenaFenceUs/1000ull));
    sVbPeak=sVbArenaFences=0;
    sVbArenaFenceUs=0;
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
    Psp_Log("psp: stage native CPU clipping: triangles=%u wholly-outside=%u preflight-rejected=%u enabled=%d cached-fans=%u\n",
            sNativeStageClipTriangles, sNativeStageClipOutside, sNativeStageClipFailed,
            gPspNativeStageClip, sNativeStageCachedClipTriangles);
    Psp_Log("psp: stage vertex projection guard: dual-matrix-rejected=%u (whole-strip VU1 fallback)\n",
             sNativeStageProjectionRejects);
    sNativeStageProjectionRejects=0;
    sNativeStageCachedClipTriangles = 0;
    sNativeStageClipTriangles = sNativeStageClipOutside = sNativeStageClipFailed = 0;
    Psp_Log("psp: GS framebuffer virtualization: saved=%u sampled-after-owner-change=%u allocation-failed=%u budget-skipped=%u active-snapshots=%u/%u recycled-stale=%u deferred=%u physical-rebind-invalidations=%u; GS alpha/indexed feedback still unavailable\n",
            sFbSnapshotsSaved,sFbSnapshotsSampled,sFbSnapshotsAllocFail,
            sFbSnapshotBudgetSkip,sFbSnapshotAllocs,(unsigned)MAX_FB_SNAPSHOTS,
            sFbSnapshotRecycled,sFbSnapshotsDeferred,sTargetRebindInvalid);
    sFbSnapshotsSaved=sFbSnapshotsSampled=sFbSnapshotsAllocFail=
        sFbSnapshotBudgetSkip=sFbSnapshotsDeferred=sFbSnapshotRecycled=0;
    Psp_Log("psp: architecture gaps: unimplemented-fb-readbacks=%u native-effects=%u (no pixels copied for FB readback)\n",
            sDiagUnimplementedFbUpload, sDiagUnimplementedNativeEffect);
    sDiagUnimplementedFbUpload = sDiagUnimplementedNativeEffect = 0;
    Psp_Log("psp: GS PSMCT16/16S direct colour: native=%u legacy-skipped=%u option=%d (depth-texture/GS-VRAM swizzle still unsupported)\n",
            sDiagFb16Native, sDiagSkip16, gPspGsFb16);
    sDiagFb16Native = 0;
    Psp_Log("psp: GS source scissor culling: empty=%u fully-outside=%u (before texture cache/GE)\n",
            sGsEmptyClipRejects,sGsPrimitiveClipRejects);
    sGsEmptyClipRejects=sGsPrimitiveClipRejects=0;
    Psp_Log("psp: GS native VU repeated static-format draw skips saved=%u\n",sGsNativeStaticSkips);
    sGsNativeStaticSkips=0;
    Psp_Log("psp: GS->GU framebuffer masks: masked-draws=%u partial-RGB=%u RGB-protected-alpha-only=%u\n",
            sGsFbMaskDraws,sGsFbMaskPartial,sGsFbMaskAlphaOnly);
    sGsFbMaskDraws=sGsFbMaskPartial=sGsFbMaskAlphaOnly=0;
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
    Psp_Log("psp: GS scene paired-screen: on=%d separate=%d main=%d other=%d routed=%u frames=%u screen-draws=%u work-draws=%u\n",
            gPspScenePairScreen, gPspScenePairSeparate, gGsMainFbp, gGsDisplayOtherFbp,
            sPairedDisplayRouted, sPairedDisplayFrames,
            sPairedScreenDraws, sPairedWorkDraws);
    sPairedDisplayRouted = sPairedDisplayFrames = 0;
    sPairedScreenDraws = sPairedWorkDraws = 0;
    Psp_Log("psp: GU swap verification: mismatch=%u unrecognized-return=%u swap_from_sdk=%d\n",
            sGuSwapMismatch, sGuSwapUnknown, gPspSwapFromSdk);
    sGuSwapMismatch = sGuSwapUnknown = 0;
    Psp_Log("psp: native PSP GU ordered passes: stage-triangles=%u fighter-triangles=%u sprites=%u transition-barriers=%u (original GS order retained)\n",
            sGuNativePassStage,sGuNativePassFighter,sGuNativePassSprites,sGuNativePassBarriers);
    sGuNativePassStage=sGuNativePassFighter=sGuNativePassSprites=sGuNativePassBarriers=0;
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
    Psp_Log("psp: indexed PSMT8 native GU T8: new=%u cache-hits=%u decoded-storage-saved=%uKB (white-sprite unchanged)\n",
            sT8NativeNew,sT8NativeHits,sT8NativeSaved/1024u);
    sT8NativeNew=sT8NativeHits=sT8NativeSaved=0;
    Psp_Log("psp: PSMT8 differential decode: checked=%u mismatched=%u rebuilt-from-scalar=%u screen-texture-lookup-preserved=%u\n",
            sT8RefVerified,sT8RefMismatch,sT8RefRebuilt,sFbReadLookupPreserved);
    sT8RefVerified=sT8RefMismatch=sT8RefRebuilt=sFbReadLookupPreserved=0;
    Psp_Log("psp: native T8 source index reuse: hits=%u misses=%u stored=%u lru-evicted=%u oversized=%u slots=%u (CLUT always live; GS source pages checked)\n",
            sT8IndexHits,sT8IndexMisses,sT8IndexStored,
            sT8IndexEvicted,sT8IndexOversize,
            (unsigned)PSP_NATIVE_T8_CACHE_SLOTS);
    sT8IndexHits=sT8IndexMisses=sT8IndexStored=0;
    sT8IndexEvicted=sT8IndexOversize=0;
    Psp_Log("psp: native T8 palette-only refresh: reused=%u in-flight-blocked=%u (source hash unchanged, GE fenced)\n",
            sT8ClutRefreshed,sT8ClutRefreshBlocked);
    sT8ClutRefreshed=sT8ClutRefreshBlocked=0;
    Psp_Log("psp: offline native T8 packs: loaded=%u not-found=%u rejected=%u (opt-in, GS page+CLUT hash checked)\n",
            sNativeT8PackLoaded,sNativeT8PackMissing,sNativeT8PackRejected);
    sNativeT8PackLoaded=sNativeT8PackMissing=sNativeT8PackRejected=0;
    Psp_Log("psp: texture cache: entries=%d/%d vram=%u KB ram=%u KB peak=%u KB hits=%u misses=%u evicted=%u alloc-failed=%u oversized=%u\n",
            sTexCount, MAX_TEX, sTexMem / 1024u, sTexRamMem / 1024u,
            sTexRamPeak / 1024u, sTexCacheHits, sTexCacheMisses,
            sTexCacheEvicted, sTexHeapFailed, sTexOversized);
    Psp_Log("psp: texture guarded reuse: hits=%u full-key=%u unrelated-upload-skips=%u epoch=%u serial=%u erase-index-repairs=%u\n",
            sTexReuseHits, sTexReuseFullKeys, sTexReuseUnrelatedUploads,
            gGsPspTextureEpoch, sTexSerial, sTexReuseEraseRepairs);
    sTexReuseHits = sTexReuseFullKeys = sTexReuseUnrelatedUploads = 0;
    sTexReuseEraseRepairs = 0;
    Psp_Log("psp: texture LOD: tex_limit=%d reduced-new=%u cache-hits=%u cache-pixels-saved=%uK\n",
            gPspTexLimit, sLodReduced, sLodCacheHits,
            (unsigned)(sLodPixelsSaved / 1024ull));
    sLodReduced = sLodCacheHits = 0;
    sLodPixelsSaved = 0;
    {
        extern unsigned GsVu1_PspStage399EmptyPerf(void);
        unsigned emptyBatches=GsVu1_PspStage399EmptyPerf();
        Psp_Log("psp: native stage399 degenerate strips: bypassed=%u (GS TEX0/PRIM replayed, no triangles)\n",
                emptyBatches);
    }
    /* PSP-specific summary from the ported Vita VIF unpack fast path.
       If few vectors use it, the next optimization should target the
       VU1 instruction interpreter rather than the VIF unpack loop. */
    {
        unsigned microPerf[3];
        extern void GsVu1_PspPredecodePerf(unsigned out[3]);
        GsVu1_PspPredecodePerf(microPerf);
        Psp_Log("psp: VU1 uploaded micro-op predecode: changed-words=%u skipped-identical=%u interpreted-instructions=%u (GS semantics unchanged)\n",
                microPerf[0],microPerf[2],microPerf[1]);
    }
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
    Psp_Log("psp: GS pre-upload FBP capture: saved-or-current=%u unsupported=%u (uses existing 1.5MiB pool, 5551 not GS RGBA32)\n",
            sGsUploadSnapshot,sGsUploadUnsupported);
    sGsUploadSnapshot=sGsUploadUnsupported=0;
    Psp_Log("psp: GS AFAIL pass: replays=%u HUD-stencil-legacy=%u; cross-PSM framebuffer aliases rejected=%u (5551 cannot encode swizzled/32-bit destination)\n",
            sGsAfailSecondPass, sGsAfailHudFallback, sFbPsmAliasRejected);
    sGsAfailSecondPass=sGsAfailHudFallback=sFbPsmAliasRejected=0;
    Psp_Log("psp: stage-to-fighter background copy: staged=%u copied=%u guarded-skip=%u (native VU1, same/previous frame, GE 5551)\n",
            sStageTagged,sStageCompositions,sStageComposeRejected);
    sStageTagged=sStageCompositions=sStageComposeRejected=0;
    Psp_Log("psp: stage compositing rejects: offscreen=%u same-surface=%u stale=%u owner-lost=%u invalid-source=%u already-copied=%u other=%u\n",
         sStageRejectOffscreen,sStageRejectSame,sStageRejectStale,
         sStageRejectOwner,sStageRejectUnready,sStageRejectAlready,
         sStageRejectOther);
    sStageRejectOffscreen=sStageRejectSame=sStageRejectStale=0;
    sStageRejectOwner=sStageRejectUnready=sStageRejectAlready=sStageRejectOther=0;
    Psp_Log("psp: fb-alias: valid=%u fallback=%u reupload=%u unrendered=%u self=%u scratch-owner-changes=%u\n",
            sFbAliasHit, sFbAliasFallback, sFbAliasReupload,
            sFbAliasUnrendered, sFbAliasSelf, sFbWorkOwnerChanges);
    Psp_Log("psp: TARGET freshness=%d invalid-rebind=%u invalid-screen-clear=%u per interval\n",
            gPspTargetFreshness, sTargetRebindInvalid, sTargetScreenCleared);
    sTargetRebindInvalid = sTargetScreenCleared = 0;
    Psp_Log("psp: GS physical WORK pair: 336-slot=0 368-slot=1 samples=%u owner-conflicts=%u owners=%u/%u texture-vram=%uKB\n",
            sWorkPairSamples, sWorkPairOwnerMisses,
            sWorkOwnerFbp[0], sWorkOwnerFbp[1], (unsigned)(TEX_SIZE/1024u));
    sWorkPairSamples=sWorkPairOwnerMisses=0;
    if (gPspLogAll) {
        Psp_Log("psp: LOG_ALL GPU-PROBE last-post-GE frame=%u samples-in-interval=%u screen-rgb=%u/%u hash=%08x work0-rgb=%u/%u hash=%08x work1-rgb=%u/%u hash=%08x stage-input=%u fighter-input=%u\n",
                sPixelProbeFrame, sPixelProbeSampleCount,
                sPixelProbeScreen.colored,sPixelProbeScreen.pixels,sPixelProbeScreen.hash,
                sPixelProbeWork0.colored,sPixelProbeWork0.pixels,sPixelProbeWork0.hash,
                sPixelProbeWork1.colored,sPixelProbeWork1.pixels,sPixelProbeWork1.hash,
                sPixelProbeScene,sPixelProbeFighter);
        sPixelProbeSampleCount=0;
    }
    Psp_DiagAllReport(); /* one bounded report for this two-second interval */
}
