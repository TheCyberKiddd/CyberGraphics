# CyberGraphics roadmap status

Snapshot: 2026-11-07

This is a factual project-state snapshot to help prepare a revised product
roadmap. The phase names and intended outcomes follow
[`graphics/CYBERGRAPHICS_ROADMAP.md`](graphics/CYBERGRAPHICS_ROADMAP.md);
this document does not replace that roadmap or change any phase checkbox.

## Executive status

- **Phases 1-14:** marked complete in [`ROADMAP.md`](ROADMAP.md), with
  implementation and evidence described in the existing phase reports.
- **Phase 15:** complete. The Intel HD Graphics 530 D3D12 sample-path bug is
  fixed, the sample submits and presents, and the fresh RenderDoc/PIX captures
  confirm the nested `Clear Pass` and `Clear backbuffer` labels and the blue
  presented frame. The raw native `SetMarker` support API remains unbundled and
  is validated through the RHI tests rather than a new external dependency.
- **Phase 16:** complete. The compiler emits HLSL used by the D3D12 cube
  comparison; Debug, Release, and Debug-ASan each pass 88/88, the package,
  structure, and layer checks pass, and the repository-wide formatter passes.
- **Phase 17:** complete. Compiler-owned
  reflection, permutations, disk cache, asynchronous compilation and pipeline
  swaps, and the D3D12 live-reload sample are implemented. Debug, Profile,
  Release, Shipping, and Debug-ASan each pass 96/96; package, structure, layer,
  formatting, and live-reload checks pass. The valid edit, syntax-error
  rejection, and restore measured 48 ms, 49 ms, and 84 ms on this PC. Its
  implementation and evidence are included in the Phase 17 commit.
- **Phase 18:** implementation and local validation complete; Debug, Release,
  and Debug-ASan each pass 98/98. The D3D12 sample self-test verifies 100
  material instances, sorted draws, and a live parameter change at frame 20.
  The package and source consumers and all structure, layer, formatting, and
  whitespace checks pass. Implementation and evidence are included in this
  commit, and the master roadmap marks the phase complete. See the
  [Phase 18 report](reports/phase-18.md).
- **Phase 19:** complete. On the Intel HD Graphics 530, the Release bindless
  path submits 10,000 unique-transform draws with p95 completion of 11.6 ms,
  under the 16.67 ms budget. Debug, Release, and Debug-ASan pass 99/99;
  package/source consumers and structure, layer, formatting, and whitespace
  checks pass. The slower dynamic-offset comparison and explicit WARP results
  are retained in the [Phase 19 report](reports/phase-19.md).
- **Phase 20:** complete. The render graph infers dependencies and barriers,
  culls unused passes, aliases compatible non-overlapping transients, and
  writes bounded text and DOT diagnostics. Debug, Release, and Debug-ASan pass
  100/100; package/source consumers and structure, layer, formatting, and
  whitespace checks pass. See the [Phase 20 report](reports/phase-20.md).
- **Phase 21:** complete. Debug, Release, and Debug-ASan pass 101/101; the Linux
  ThreadSanitizer render-thread and hot-pipeline targets pass; package/source
  consumers and repository checks pass. See the
  [Phase 21 report](reports/phase-21.md).
- **Phase 22:** complete; scene proxies and culling are documented in the
  Phase 22 report.
- **Phase 23:** complete; geometry processing and indirect batches are
  documented in the Phase 23 report.
- **Phase 24:** implementation and local validation complete. The Phase 24
  report records parser/cooker tests, fuzz mutations, benchmark, and known
  limits.
- **Phases 25-44:** not started.
- **Current PC:** Windows 10 Pro 22H2, Intel Core i5-6400 (4 cores), Intel HD
  Graphics 530 (1 GiB reported), Intel driver `31.0.101.2125` dated
  2023-05-24. The marker sample now submits and presents on this adapter after
  fixing an RTV descriptor-heap mismatch; this is not a broad hardware matrix.
- **Visual progress on this PC:** the software-rendered textured cube works.
  With the new 1x default, a two-frame 1280x720 smoke run measured 35.1 FPS; a
  deterministic 640x360 capture succeeded. This does not exercise the GPU.
- **D3D12 sample:** a native probe created a device, direct queue, command
  allocator, and command list. The sample's backbuffer RTV had used the
  shader-resource heap and the wrong heap kind; this was corrected to use the
  render-target heap. The initial `DXGI_ERROR_DRIVER_INTERNAL_ERROR`
  (`0x887A0020`) no longer reproduces, and the sample submits and presents.
- **WARP investigation:** automated WARP-backed RHI tests pass. A temporary
  windowed marker-sample attempt crashed in `d3d10warp.dll` with
  `0xC0000005`; no WARP capture was produced. This is a separate historical
  limitation, not the current Intel hardware result.
- **Capture tools:** PIX 2610.28 is installed and portable RenderDoc 1.48 is
  available outside the repository. RenderDoc shows `Clear Pass` with
  `Clear backbuffer` beneath it and the blue presented frame; PIX shows the
  nested `Clear Pass` region and the blue presented frame, while the raw native
  point-marker call remains hidden as a separate visible UI event. PIX event-list
  export requires Windows Developer Mode, which remains disabled.

## Phase-by-phase state

### Part 1 — Foundation

| Phase | Status | Delivered / remaining |
|---|---|---|
| 1. Repository and build system | Complete | CMake, presets, compiler settings, scripts, tests, and CI. |
| 2. Core utilities | Complete | Types, errors/results, assertions, logging, build configuration, and test runner. |
| 3. Math library | Complete | Vector/matrix and geometry math, transforms, conventions, and tests. |
| 4. Memory and containers | Complete | Allocators, containers, handles, and sanitizer coverage. |
| 5. Platform layer and job system | Complete | Windows platform/window services, timing, input, threads, and jobs. |

### Part 2 — CPU software renderer

| Phase | Status | Delivered / remaining |
|---|---|---|
| 6. Framebuffer and present | Complete | CPU framebuffer, conversion/present path, pacing, and frame-time display. |
| 7. Triangle rasterization | Complete | Line/triangle rasterization, top-left coverage, tiled jobs, and tests. |
| 8. Depth, clipping, and perspective | Complete | Reverse-Z depth, homogeneous clipping, camera, perspective interpolation, and tests. |
| 9. Textures, CPU shaders, and test scenes | Complete | Texture sampling, CPU shader/lighting, mesh loading, five golden images, and interactive textured-cube sample. Current machine's measured 1x smoke run: 35.1 FPS at 1280x720 for two frames. |

### Part 3 — RHI and Direct3D 12

| Phase | Status | Delivered / remaining |
|---|---|---|
| 10. RHI design | Complete | RHI object model, capabilities, backend boundary, and null backend. |
| 11. Resources | Complete | Buffers, textures, views, samplers, format table, and resource states. |
| 12. Pipelines and binding model | Complete | Shader modules, binding groups, pipeline layouts/state, and cache. |
| 13. Command lists, synchronization, swapchain | Complete | Recording, render passes, barriers, queues/fences, frame ring, retirement, and swapchain contract/tests. |
| 14. Direct3D 12 backend | Complete by recorded Phase 14 evidence | D3D12 adapter/device/queues, resources, shaders/bindings, pipelines, recording/submission, swapchain, and image comparison against software output. The Phase 15 sample-path RTV heap-kind mismatch on the Intel host is fixed; hardware coverage remains limited to this adapter. |
| 15. GPU memory and debug tooling | Complete | Heap suballocation/transient reuse, validation, object naming, native markers, timestamps, leak reporting, and device-loss diagnostics. Automated test matrix passes. RenderDoc and PIX captures confirm the nested `Clear Pass` and `Clear backbuffer` labels and the blue presented frame on the Intel HD 530 after the RTV heap-kind fix. |

### Part 4 — Shaders and materials

| Phase | Status | Intended outcome |
|---|---|---|
| 16. Shader language and compiler strategy | Complete | ADR 0019, the language design, compiler, source diagnostics, tests, and generated-HLSL cube integration are complete; Phase 16 evidence is recorded in its report. |
| 17. Reflection, permutations, and hot reload | Complete | Compiler-owned reflection, bounded/stable permutations, checksummed cache, job-system compilation, reflected renderer layouts, async frame-boundary swaps, and D3D12 live reload. All five Windows presets pass 96/96; the valid edit, syntax rejection, and restore pass in 48/49/84 ms on the Intel HD 530. |
| 18. Material system | Complete | Immutable material definitions, mutable reflected instances, texture slots, explicit uploads, sorted queues, `.cybermat` serialization, and a 100-instance D3D12 sample; evidence is in the Phase 18 report. |
| 19. Constant streaming and bindless resources | Complete | Fence-safe frame streaming, dynamic offsets, descriptor allocators, capability-gated bindless/fallback paths, and a 10,000-draw Intel HD 530 benchmark meeting the 16.67 ms frame budget; Debug, Release, Debug-ASan, package/source, and repository checks pass. See the Phase 19 report. |

### Part 5 — Renderer architecture

| Phase | Status | Intended outcome |
|---|---|---|
| 20. Render graph | Complete | Inferred dependencies and barriers, pass culling, compatible transient aliasing, bounded text/DOT output, and a tested multi-pass frame; Debug, Release, Debug-ASan, package/source consumers, and repository checks pass. See the Phase 20 report. |
| 21. Render thread and frame pipelining | Complete | Bounded frame handoff, render-owner RHI access, profiler-proven overlap, and sanitizer/stress evidence. See the Phase 21 report. |
| 22. Scene, render proxies, and culling | Complete | Copied proxies, parallel culling, screen-space LOD selection, and committed Debug/Release/ASan, sanitizer, package/source, benchmark, and repository-check evidence. See the Phase 22 report. |
| 23. Geometry pipeline | Complete | Mesh processing, packed formats, LODs, and indirect batches; Debug/Release/ASan pass 106/106, the million-triangle LOD render and RHI indirect submission pass, and package/repository checks pass. See the Phase 23 report. |
| 24. Asset import and cooked render assets | Complete | In-house importers/decoders, versioned cooked formats, asset database/cooker, hot reload, and bounded parser mutation tests. See the Phase 24 report for benchmark and local validation evidence. |
| 25. Engine-facing renderer interface | Not started | Stable versioned C plugin boundary, capabilities, scene/view/light/material API, and software-renderer implementation. |

### Part 6 — Lighting and visual quality

| Phase | Status | Intended outcome |
|---|---|---|
| 26. Physically based forward renderer | Not started | Metallic-roughness BRDF, lights, HDR, tone mapping, and reference comparison. |
| 27. Clustered lighting and deferred option | Not started | Clustered light culling, optional G-buffer path, transparency policy, and 1,000-light measurements. |
| 28. Shadows | Not started | Cascaded and local shadow maps, atlas/filtering/bias, and artifact-tested scene. |
| 29. Image-based lighting and reflections | Not started | Environment lighting, prefiltering, probes, screen-space reflections, and sky. |
| 30. Post-processing and anti-aliasing | Not started | Exposure, tone mapping, bloom, AO, depth of field, motion blur, grading, and AA. |
| 31. Global illumination and ray tracing | Not started | Choose and implement an initial GI path; hardware ray tracing stays optional/capability-gated. |

### Part 7 — Scale, backends, and performance

| Phase | Status | Intended outcome |
|---|---|---|
| 32. GPU-driven rendering and compute | Not started | GPU culling, indirect draws, optional mesh shaders, and async compute. |
| 33. Texture compression and streaming | Not started | BCn compression, budgeted mip streaming/residency, and background loading. |
| 34. Vulkan backend | Not started | Second explicit backend, synchronization/presentation/validation, and Linux/Android support. |
| 35. Metal and other backends | Not started | Metal for Apple platforms; evaluate WebGPU/console only with deliberate scope/access. |
| 36. Performance hardening | Not started | CPU/GPU/memory budgets, frame-time percentiles, pipeline warmup, and CI regression benchmarks. |
| 37. Specialized rendering features | Not started | Prioritized skinning, particles, decals, terrain, water, volumetrics, vegetation, 2D/UI, and debug drawing. |

### Part 8 — Tools, quality, and shipping

| Phase | Status | Intended outcome |
|---|---|---|
| 38. Debug UI and in-engine profiling | Not started | First-party overlay, timelines, console variables, and render-graph viewer. |
| 39. Frame capture and debugging tools | Not started | First-party frame recording/replay/inspection alongside PIX and RenderDoc. |
| 40. Automated testing | Not started | Golden-image/backend matrix, fuzz/stress/long-run/crash testing, NVIDIA/AMD/Intel hardware coverage. |
| 41. Editor viewport integration | Not started | Editor rendering, picking, outlines, gizmos, previews, and thumbnails. |
| 42. Documentation, samples, and public API stability | Not started | API/tutorials, ten samples, and version/deprecation policy. |
| 43. Packaging, licensing, and distribution | Not started | SDK/installer/update/distribution, notices, and legal review. |
| 44. Beta program and release | Not started | Alpha/beta support process, compatibility matrix, release gate, and launch plan. |

## Known issues and roadmap decisions to revisit

1. **Hardware coverage is still narrow.** The clear-pass sample now submits and
   presents on the local Intel HD 530, but this single machine does not
   establish a supported adapter/driver matrix or performance targets.
2. **There is no automatic renderer fallback.** The CPU renderer is a separate
   product/sample, not a selectable backend for the D3D12 marker sample. The
   stable engine-facing selection interface is planned for Phase 25. A revised
   roadmap should decide whether a software fallback or a D3D11 backend is a
   product requirement and schedule it explicitly; neither exists today as an
   automatic fallback for this sample.
3. **The windowed WARP path remains unverified.** Tests using WARP pass, but a
   temporary windowed sample attempt crashed inside `d3d10warp.dll`. Do not rely
   on WARP as an interactive fallback based on the RHI tests alone.
4. **Capture evidence is scoped.** PIX and RenderDoc show the nested `Clear
   Pass` and `Clear backbuffer` labels and the presented blue frame. The raw
   native point-marker API is validated by RHI tests but is not exposed as a
   separate visible PIX event; this does not block Phase 15 acceptance.
5. **Current software-renderer performance is modest.** The 35.1 FPS figure is
   a short two-frame sample smoke measurement, not a benchmark. Add repeatable
   performance measurements before setting low-end device targets.
6. **Phase 40 hardware matrix is still future work.** Introduce a small
   GPU/driver compatibility test and a fallback acceptance target well before
   advanced rendering work.
7. **Scope and maturity differ.** Phases 1-24 are marked complete, but this
   repository is still a renderer foundation: the engine-facing public plugin
   API (Phase 25), full PBR lighting, editor integration, and
   shipping workflow are not implemented.

## Verified validation and worktree notes

- On the Phase 18 implementation, Debug, Release, and Debug-ASan each passed
  **98/98** tests. The Release installed-package consumer and Debug
  source-tree consumer both built and ran.
- The D3D12 material sample self-test passed on the Intel HD Graphics 530:
  100 definitions, 100 instances, 100 sorted draws, and a live parameter
  change at frame 20.
- Structure, layer, formatting, and `git diff --check` passed.
- Phase 18 code, documentation, package rules, and validation evidence are
  committed in `679a64c` (`renderer: add Phase 18 material system`).
- Phase 21 implementation and validation evidence are recorded in the
  [Phase 21 report](reports/phase-21.md). The Linux TSan preset's Clang 21
  warning-policy exception is documented there.
- `diagnosing-low-phase-6-sample-frame-rate.json` is unrelated and untracked;
  leave it alone.

## Immediate next step

Phase 24 is complete. Phase 25 (engine-facing renderer interface) is next in
roadmap order. See the [Phase 24 report](reports/phase-24.md) for exact
validation and known gaps.
