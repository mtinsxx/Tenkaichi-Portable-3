# Why BT3-PSP slows across menus, opening and battles

## Where the port spends time, not just one fighter VU1 program
The PR #39 port uses a **synchronous** PSP GS/VIF1 translation:
`src/sys/dma.c:Dma_Flush` -> `port/src/gs/gs_core.c:
Port_GsVif1Chain` -> `run_chain` -> `Gs_Gif` ->
`port/psp/psp_gu.c:GsGpu_Draw`. Unlike PS Vita's threaded
queue/snapshot backend, PSP currently executes the expensive
translation on the game's CPU. The CPU clock is ALREADY set
to 333 MHz in `psp_main.c`; there is no missing clock setting.

The user reported slowdown in **all** screens. On their Oct 26
20:42 log: early menus ~40-55 real GS FPS and movie some 8-22
FPS intervals, while later scene/character selection ~3-7 and
battle ~1-2. These are DIFFERENT workload intensities, not
evidence that one VU1 optimization fixes every screen.
Combat GSchain generally occupies most of the 2s interval;
PSP GU frame finish sync ~=0ms, BUT eviction sync was never
measured by the existing GUfinishSync timer.

## Root issue in `psp_gu.c`: memory lifecycle and accidental GE stalls
Prior implementation:
1. `texture_get` dynamically allocates a huge temporary
   512x512x4 RGBA staging buffer on each cache miss (plus CLUT
   512x512 index staging for indexed textures). It frees both
   immediately. Under high cache pressure (newlib pool shared
   with game heap, AFS/ELF/movie), `malloc` fails and
   `tex_malloc_pressure` repeatedly evicts cached textures.
2. **EVERY** `tex_evict_one`, even an LRU used 100 frames ago,
   performed `flush(); sceGuFinish(); sceGuSync()`. Yet
   `GsGpu_FrameEnd` already finishes+syncs the full GE list
   BEFORE incrementing `sFrame`. Therefore last-used-frame <
   current-frame textures are provably not referenced by any
   currently running GE draw. Frequent unnecessary CPU/GPU
   synchronization is a real code bug, independent of graphics
   quality and which game screen is active.
3. The user's long log reaches 49,000+ cumulative texture
   evictions, 1,400+ scratch malloc failures, and thousands of
   recovery cycles. This is not solved by stage/fighter batching.

## PR40 changes (all screens; no resolution/quality changes)
- Only when victim.last < sFrame skip redundant GE
  finish/sync. Same-frame or invalid/future timestamps still
  call the exact old safe flush/finish/sync.
- Allocate 1 MiB reusable RGBA and 256 KiB reusable indexed
  staging arrays ONCE in PSP-GU init before texture pressure.
  If reservation fails, transparently use old malloc/eviction.
- Subtract reserved staging bytes from the **total** nominal
  RAM texture-cache ceiling (10 MiB), keeping total memory
  within prior bounds. All pixels are decoded afresh; cache
  content/key, TEX0/CLUT, UV, LOD 256 and output textures
  are unchanged.
- A/B scratch opt-out: `texture_scratch=0` (optional bt3.txt),
  while old-frame GE-fence fix remains active for safety.
- Logs: `shared texture scratch` and `texture lifetime:
  old-frame-no-GE-wait=N current-frame-GE-fence=N
  fence-time=M ms scratch-rgba=N scratch-idx=N`.
- Host tests for GPU texture lifetime fence requirement,
  no cache-budget overflow and scratch boundary sizes.

## What is NOT claimed
This patch is one verified systemic source of unnecessary
waiting and repeated allocation. It does NOT make all scene
rendering asynchronous and cannot by itself solve full PS2
graphics workload on 333-MHz PSP. PPSSPP tests must compare
same menu, selection, opening and battle FPS against PR39;
watch new counters and regressions. If cold evictions are
rare, the change will provide little FPS gain: continue
profiling `Gs_Gif` parser, GS batch count and MPEG software
decode instead of repeating fighter VU1 patches.

## Also fix avoidable synchronous logging in all screens
The PSP boot code defaulted to `sLogFlush=1`, calling `fflush` for
**each** of ~40+ lines during every two-second report interval. File
writes can block the same CPU thread responsible for game/GS progress.
We now flush once after each complete report, and immediately before
starting the game, on fatal errors, and on exit. The log still covers
every interval; the most recent incomplete ~2 seconds may be lost in an
abrupt hardware/power crash, so `log_flush=1` restores the previous
per-line durability for debugging. This improves normal operation in
menus, movies, selection and fights, independent of VU1 optimizations.
