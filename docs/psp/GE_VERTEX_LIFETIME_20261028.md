# PSP GE vertex lifetime correction (28 October 2026)

## Bug identified in source

The previous PSP backend allocated one `MAX_QUEUE` array and used `sceGuDrawArray(..., sVb)` for each GS state/primitive batch. `sceGuDrawArray` enqueues a GE pointer, **not a private copy** of the vertex data. `flush()` immediately set `sVbCount=0` and overwrote that same array for the next batch before `sceGuFinish/sceGuSync`. Therefore older in-flight commands could sample vertices and color/UV coordinates from an unrelated later batch. That is a concrete lifetime bug consistent with intermittent giant triangles, sprite corruption and texture-space flicker.

## Implementation

- Replace one mutable vertex scratch array with a GE-readable, 64-byte aligned main-RAM arena. Each `flush()` enqueues a distinct non-overlapping vertex span and advances the cursor.
- Recycle only after the normal `GsGpu_FrameEnd` GE sync or a pre-existing texture eviction fence.
- If an unusually complex frame exhausts the arena, finish/sync and restart the GE list before reusing memory, preserving GS state order. Keep the old `MAX_QUEUE` per-batch bound.
- Try 96K, 48K, 24K, 12K vertex capacity on increasingly memory-constrained PSPs; at every size, waiting safely is preferred to reusing memory early. No disc/assets/ELF are stored in the repository.
- Report high water mark, arena fences and time spent fencing.
- Host test `test_vertex_arena.c` checks nonoverlapping lifetime logic and is run in both PSP CI workflows.

**Limits:** this patch directly fixes GPU vertex pointer aliasing, but screenshots must verify whether the triangles and white/color bands disappear. The GS framebuffer translation for PSMCT32/PSMT8, native post effects and FPS bottlenecks remain independent known issues. The PSP hardware cannot guarantee 60 FPS simply from this memory fix.

Branch includes prior color/font baseline (`fe93677`), stage guards and GE VRAM address work. Test BUILD-ID `TP3-GE-VERTEX-LIFETIME-20261028`. Compare the same battle on PPSSPP; check `GE vertex arena` report and capture a new log if artifacts persist.
