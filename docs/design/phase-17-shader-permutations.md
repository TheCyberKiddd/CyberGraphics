# Phase 17 — Reflection, permutations, and hot reload

Date: 2026-11-05
Status: accepted design
Related: [ADR 0020](../adr/0020-shader-reflection-permutations-and-reload.md),
[ADR 0019](../adr/0019-shader-strategy.md),
[reading notes](../notes/phase-17.md),
[cyber_shaders contract](../../engines/cyber_shaders/docs/MODULE.md),
[cyber_renderer contract](../../engines/cyber_renderer/docs/MODULE.md)

## Goal

Turn the Phase 16 one-shot compiler into a shader system a running renderer can
use: reflection that is the single source of truth for bindings, shader
keywords with a bounded permutation count, an on-disk cache, compilation on the
job system, asynchronous pipeline creation with a fallback, and hot reload that
swaps pipelines at a frame boundary.

Done when: editing a shader while the sample runs updates it within about a
second, a syntax error keeps the previous shader running, and the permutation
report is generated.

## What changes, by module

### `cyber_shaders` (no RHI dependency, unchanged layer)

1. **Reflection.** `CompileHlsl` gains a `CompileOptions` overload and
   `CompiledShader` gains a `Reflection`:
   - stage inputs and outputs (name, type, location or builtin);
   - resources (constant buffer, sampled texture, sampler, storage texture)
     with register class, index, and space;
   - constant-buffer layouts: every member's byte offset and size using the
     HLSL packing rules, and the buffer's 16-byte-rounded size;
   - compute thread-group size.
   Names are copied into fixed 64-byte slots so reflection never borrows the
   source text and can be cached and serialized.
2. **Constant buffers.** Uniforms that name the same `@register(bN, spaceM)`
   now form one constant buffer in declaration order. Every emitted member
   carries `packoffset(cR.comp)`, so the HLSL layout equals the reflected one
   by construction.
3. **Compute stage.** `shader compute Name @threads(X, Y, Z) { ... }` with a
   `uint3` input bound to `@builtin(dispatch_thread_id)`, `rwtexture2d`
   storage textures (`u` registers), and a `store(target, input.id, value)`
   statement that writes `target[id.xy]`. Limits: each dimension >= 1,
   X, Y <= 1024, Z <= 64, X*Y*Z <= 1024 (cs_5_x).
4. **Keywords.** `keyword NAME;` at shader scope declares a boolean shader
   keyword (at most 8 per shader, so at most 256 permutations).
   `when NAME { ... } else { ... }` selects declarations and statements. Both
   branches are always lexed and parsed, so a syntax error anywhere in the file
   is reported regardless of the selected permutation; only the selected branch
   is type-checked and emitted. Enabling an undeclared keyword is an error.
5. **Permutation keys.** `PermutationKey` holds a bit mask over the declared
   keyword order and a stable 64-bit hash: xxHash64 of the enabled keyword
   names sorted by byte value, combined in that order. The hash does not depend
   on declaration order, on the order the caller lists keywords, or on the
   machine. `ScanKeywords(source)` lists a source's keywords without compiling.
6. **On-disk cache.** `ShaderCache` stores one file per key under a caller
   directory. The key is the source hash, the compiler version
   (`kCompilerVersion`), the permutation hash, and the target (`"hlsl-sm5.1"`).
   The file is a versioned little-endian header (magic `CGSC`, format version,
   the full key, payload size, xxHash64 of the payload) followed by the
   serialized HLSL and reflection. Loading treats the file as hostile: every
   size is bounds-checked, a mismatched key or checksum is a miss, and a torn
   write is a miss. A miss is never an error for the caller.
7. **Shader library.** `ShaderLibrary` owns registered shader files, compiles
   every permutation a caller requests on `cyber_jobs`, consults the cache, and
   watches the files with `platform::FileWatcher`. `Update()` runs on the
   owning thread, polls the watcher at most every 50 ms, submits recompiles,
   and publishes finished results as versioned events. A failed compile keeps
   the last good result, records the diagnostic, and is logged; the result
   version is unchanged. `WritePermutationReport` lists, per shader, the stage,
   declared keywords, possible permutations, requested permutations, compiled
   permutations, cache hits, and the last error.
   Worker threads allocate through a private mutex-guarded adapter around the
   caller's allocator, because `memory::Allocator` is not thread-safe.

### `cyber_renderer` (activated: planned -> active)

The renderer is the first module allowed to see both the RHI and the shader
compiler, so the glue lives here (ADR 0020).

1. **Reflected layouts.** `BuildReflectedLayout(stages)` merges the reflection
   of every stage of a program into bind-group layout entries and push-constant
   ranges using the backend binding convention recorded in ADR 0017:
   register space `S` is bind-group slot `S`; sampled and storage resources take
   `t`/`u` ordinals and samplers `s` ordinals in register order and must be
   contiguous from 0; the constant buffer at `b0, space0` becomes the push
   constant range (at most 128 bytes). Any other constant buffer is rejected as
   `Unsupported` until Phase 19 adds constant streaming. Visibility is the union
   of the stages that declare the resource; a register used with two different
   kinds is rejected. `BuildVertexLayout` derives interleaved vertex attributes
   from the vertex stage inputs in location order.
2. **Hot pipelines.** `HotGraphicsPipeline` owns one graphics pipeline built
   from a vertex and pixel `CompiledShader` plus a caller-supplied render-state
   template. `Rebuild` submits pipeline creation (shader modules, layout,
   pipeline) to the job system and returns immediately. `BeginFrame(frame)`
   is the frame boundary: it publishes a finished build, retires the previous
   objects, and destroys retired objects once `frame` is past the caller's
   frames-in-flight window. Until the first build succeeds, `Current()` returns
   the caller's fallback pipeline; a failed build keeps the previous pipeline.

### Sample `06_shader_hot_reload`

A D3D12 window draws a triangle from `shaders/triangle_vertex.cgs` and
`shaders/triangle_pixel.cgs`. The sample watches copies beside the executable,
so an editor save is picked up live. The pixel shader declares `TINT`; pressing
`T` switches between its two compiled permutations. `--self-test` rewrites a
copy of the pixel shader, measures the time until a new pipeline is published,
injects a syntax error and checks that the current pipeline handle and
generation remain unchanged, restores the valid source, writes the permutation
report, and exits non-zero if any step fails or exceeds one second.

## Threading and lifetime

- `ShaderLibrary` and `HotGraphicsPipeline` are owned by one thread (the render
  thread from Phase 21; the main thread now). Only their jobs run elsewhere, and
  jobs touch only job-owned state plus a mutex-protected completion slot.
- The render thread never waits on compilation: `Update` and `BeginFrame` only
  inspect finished work. Destructors wait for their own outstanding jobs.
- Pipelines are swapped only in `BeginFrame`, so a command list never sees two
  versions of the same program.

## Tests

- Reflection: IO, resource registers, constant-buffer offsets for the Microsoft
  packing examples, compute thread-group sizes and their limits.
- Keywords: selection, undeclared/duplicate keywords, limit, stable hashing
  independent of order, syntax errors inside inactive branches.
- Cache: round trip, key mismatch, version mismatch, corrupt payload,
  truncated file, oversized sizes, and a deterministic corruption sweep.
- Library: parallel compilation on 1, 2, 4, and 8 workers, cache hits on a
  second library, reload on file change, error retention, and the report.
- Renderer: reflected layouts for the cube, merged visibility, conflicting
  registers, gaps, oversize push constants, and a null-backend hot pipeline
  that falls back, swaps at a frame boundary, keeps the old pipeline on failure,
  and retires objects after the frames-in-flight window.
- The D3D12 `backend_compare` cube builds its layout from reflection. The live
  D3D12 sample self-test measures valid edit, syntax-error retention, and restore
  latency on the current Windows adapter.

## Non-goals

- Backend bytecode (DXBC/DXIL) caching; the RHI compiles HLSL inside
  `CreateShader` and has no bytecode export yet. Recorded as a gap.
- Constant buffers other than the push-constant block (Phase 19).
- Materials and parameter blocks (Phase 18).
- `#include`, macros, user functions, and other language growth not needed by
  the sample.
- A directory-change (`ReadDirectoryChangesW`) watcher; see the notes.
- Changes to the stable C ABI.

## Sources

Microsoft HLSL packing rules, `packoffset`, `numthreads`, and
`ReadDirectoryChangesW` documentation; the Khronos Vulkan specification
(`maxPushConstantsSize`); the xxHash specification. See the reading notes.
</content>
