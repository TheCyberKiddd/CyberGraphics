# Changelog

All notable changes to CyberGraphics are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
semantic versioning once a public API exists.

## [Unreleased]

### Added

- Phase 11 `cyber_rhi` resources: `BufferUsage`/`BufferDesc`/`BufferInfo`,
  `TextureDimension`/`TextureUsage`/`TextureAspect`/`SubresourceRange`,
  `TextureDesc`/`TextureViewDesc`, and `SamplerDesc` with every filter, address,
  border, LOD, anisotropy, and comparison field a D3D12 or Vulkan sampler needs.
- A numeric format table: `FormatInfo`, `FormatInfoOf`, per-field accessors,
  sRGB/linear variant lookup, view compatibility, and multisample, render-target,
  storage, and blend support rules. `docs/formats.md` renders the table and a
  test walks every enumerator.
- A resource state vocabulary (`ResourceState`, `BufferBarrier`,
  `TextureBarrier`, `ResolveSubresourceRange`) that maps to D3D12 resource states
  and Vulkan image layouts, with read states that combine and at most one write.
- `Device` resource creation and destruction for buffers, textures, views, and
  samplers; `WriteBuffer`/`ReadBuffer` host access; `BufferStateOf` and
  `TextureStateOf`; and `Transition`, which validates a whole batch before
  applying any of it.
- Validation of every descriptor in the null backend: usage combinations,
  extents, mip chains, cubes, arrays, multisampling, limits, format capability,
  view compatibility, sampler LOD and anisotropy, and stale-handle rejection.
  Views are retired with their texture, and host-visible bytes actually
  round-trip.
- `rhi_format_table`, `rhi_buffer`, `rhi_texture`, `rhi_sampler`, and
  `rhi_resource_state` test binaries; the installed-package and source consumers
  exercise buffers, textures, views, samplers, and barriers.
- ADR 0014, the Phase 11 design note, `docs/formats.md`, and the Phase 11 report.
- Phase 12 `cyber_rhi` pipelines and binding: shader modules with stage,
  bytecode format, and entry point; four binding slots fixed by change frequency;
  group layouts with type, visibility, and array count; bind groups with
  flattened resources; pipeline layouts with group slots and up to four
  push-constant ranges; complete graphics and compute pipeline descriptions;
  and a pipeline cache object.
- Stable `HashGraphicsPipelineDesc`/`HashComputePipelineDesc` over every field
  that changes the compiled object, excluding labels.
- Validation of shader stages, layout entries and collisions, group resource
  counts and types, buffer ranges and alignment, texture and sampler usage,
  vertex bindings and attributes, rasterizer, blend, multisample, and
  depth-stencil state, attachment formats, and sample counts.
- `R32G32B32Float` joins the format table for three-component vertex positions.
- `rhi_shader`, `rhi_binding`, `rhi_pipeline`, and `rhi_pipeline_hash` test
  binaries; the package consumer creates a shader, a layout, a group, and a
  graphics pipeline through the exported target.
- ADR 0015, the Phase 12 design note, and the Phase 12 report.
- Phase 13 `cyber_rhi` command recording: `CommandList` with `Begin`/`End`/
  `Reset`, render pass begin and end, pipeline and group binding, vertex and
  index buffers, viewport and scissor, draws, indexed draws, indirect draws,
  dispatches, buffer and texture copies, color and depth clears, in-list
  barriers, push constants, and balanced debug labels.
- `RenderPassDesc` with load and store operations, reverse-Z clear values, resolve
  targets, read-only depth rules, and a pure validator both the RHI and the
  backend call.
- `SubmitDesc` with one-call submission per queue, timeline `FenceSignal` and
  GPU-side `GpuWait` entries, and validation that refuses a deadlock expressed
  as data.
- `FrameRing`, a policy object that hands out frame slots and fence values and
  waits on the slot being reused, so no resource is touched by two frames at
  once.
- `RetirementQueue`, deferred destruction keyed by fence value for every object
  kind, with collection and flush paths.
- `SwapChain` with acquire, present, resize, fifo/mailbox/immediate present modes,
  device-owned images, and stale image handles after a resize.
- Null-backend command recording, submission, and swapchain: a per-list shadow of
  resource state that validates every command in order, clear messages that name
  the missing barrier, and submission that applies waits, reaches signals, and
  commits a list's transitions when it ends.
- `rhi_render_pass`, `rhi_command_list`, `rhi_submit`, `rhi_missing_barrier`,
  `rhi_frame_ring`, `rhi_retirement`, and `rhi_swapchain` test binaries; the
  package consumer records a frame, submits it, acquires and presents an image,
  and proves a missing barrier is caught through the installed package.
- ADR 0016, the Phase 13 design note with the frame lifecycle diagram, and the
  Phase 13 report.
- Repository skeleton: `engines/`, `include/`, `tools/`, `samples/`, `tests/`,
  `docs/`, `cmake/`, `scripts/`, `assets/`, and `third_party/`.
- Sixteen engine modules with written contracts in `engines/MODULES.md` and
  `engines/<module>/docs/MODULE.md`.
- Stable product contract location at `include/cybergraphics/`.
- CMake 3.28+ build with Debug, Release, Shipping, ASan, and CI presets.
- Module helper (`cyber_add_module`, `cyber_add_test`) and compiler policy.
- Structure and dependency-law checks (`scripts/check_structure.py`,
  `scripts/layer_check.py`).
- Windows and Linux CI workflow.
- Architecture, maths conventions, coding standard, ADR, testing, and
  performance documentation.
- Project requirements note and the Phase 1 design note, phase report, and
  ADRs 0001 (module architecture) and 0002 (math and depth conventions).
- `cyber_core` reference module with version metadata and a CTest smoke test.
- Install and CMake package export: `find_package(CyberGraphics CONFIG)` with
  `cyber::core` and `cyber::graphics_api` targets.
- `tests/integration/package_consumer` verifies the installed package on
  Windows and Linux CI.
- Single-source version generation from `project(VERSION)`.
- Explicit MSVC runtime selection (`/MD` by default, `/MT` option).
- Formatting gate (`scripts/check_format.py`), SHA-pinned GitHub Actions, and
  Dependabot updates.
- Integration guide, ADR 0003, and `SECURITY.md`.
- Phase 2 `cyber_core` primitives: fixed-width types, `BuildConfig`, `Error`,
  `Result<T, E>`, assertions, a bounded logger with sinks, and xxHash64.
- Project test runner in `tools/test` with `CG_TEST`, `CG_EXPECT_*`,
  `CG_REQUIRE`, listing, filtering, repeat, seed, shuffle, and console/TAP/JUnit
  output.
- ADR 0004, the Phase 2 design note, and the Phase 2 report.
- Phase 3 `cyber_math`: vectors, column-major matrices, quaternions, TRS
  authoring, right-handed reverse-Z view/projection matrices, AABB/plane/ray/
  sphere/frustum geometry, linear/sRGB color, and IEEE binary16 storage.
- Compile-time SSE2 `Vec4` fast path with a scalar fallback and SIMD-versus-
  scalar benchmark (`cyber_math_bench`).
- Absolute-tolerance test assertions: `CG_EXPECT_NEAR` and `CG_REQUIRE_NEAR`.
- ADR 0005, the Phase 3 design note, and the Phase 3 report.
- Phase 4 `cyber_memory`: the `Allocator` interface, `HeapAllocator`, linear,
  stack, pool, free-list, ring, frame, and tracking allocators, plus tagged leak
  reports and allocator accounting.
- `Span`, `StringView`, `String`, `DynamicArray`, `FixedArray`, open-addressing
  `HashMap`, `Handle<Tag>`, and generation-checked `SlotMap<T, Tag>`.
- Deterministic 128-block free-list stress coverage, deliberate-leak coverage,
  and stale-handle coverage in the memory tests.
- `cyber::memory` install/export and source/package consumer coverage.
- ADR 0006, the Phase 4 design note, and the Phase 4 report.
- UBSan builds exclude the `vptr` check because runtime code and tests share
  RTTI-disabled settings; test targets keep exceptions enabled.
- Phase 5 `cyber_platform`: renderer-agnostic windows, fixed-size event queues,
  keyboard/mouse input, native high-resolution clocks, frame timer,
  fixed-timestep helper, UTF-8 file I/O, read-only memory mapping, path helpers,
  polling file watching, native threads, synchronization wrappers, thread
  naming, and dynamic-library loading. Win32 lives under `src/win32/`; other
  platforms get a null window backend.
- Phase 5 `cyber_perf`: non-allocating `Profiler` interface, `NullProfiler`, and
  a process-lifetime null instance.
- Phase 5 `cyber_jobs`: fixed worker pool, bounded per-worker work-stealing
  deques, dependency counters, generation-checked handles, `SubmitDetached`,
  `Wait`, `WaitIdle`, `ParallelFor`, profiler scope markers, and deterministic
  shutdown.
- `samples/00_window` proves the platform layer runs without a renderer.
- `cyber_jobs_bench` measures 10,000 jobs at 1, 2, 4, and hardware worker
  counts and reports wall time, speedup, and throughput.
- `cyber::platform`, `cyber::perf`, and `cyber::jobs` install/export and are
  exercised by the source and package consumers.
- ADR 0007, the Phase 5 design note, and the Phase 5 report.
- MSVC builds define `_HAS_EXCEPTIONS=0` for every target so standard-library
  exception ABIs agree under shipping LTO; tests keep `/EHsc` unwinding.
- Phase 6 `cyber_software_renderer`: linear `RGBA8Unorm`/`RGBA32F` framebuffer
  with clipped fills, pixel access, blits, and format conversion; sRGB present
  packing through a 16 KiB lookup table (within one 8-bit code of
  `EncodeSrgb8`); deterministic PPM and TGA export; a 5x7 bitmap font; and a
  60 Hz `FramePacer` with catch-up clamping, frame statistics, and a bounded
  1.5 ms spin that cancels OS timer rounding.
- Phase 6 `cyber_platform`: allocation-free `Presenter` seam over caller-owned
  RGBA8 bytes with a Win32 `StretchDIBits` backend (BGRA swizzle,
  allocator-backed scratch, fixed-point nearest sampling), plus
  `SleepUntilSeconds`/`SleepSeconds` built on a per-call high-resolution
  waitable timer on Windows and `nanosleep` on POSIX.
- `samples/01_gradient` shows an animated linear gradient at 60 Hz with a
  frame-time overlay, `F2`/`F11`/`Escape` controls, a `--frames` smoke run that
  prints the per-frame cost breakdown, and a `--capture` mode that proves two
  images of the same frame are byte-identical.
- `cyber_software_renderer` installs/exports and is exercised by the source and
  package consumers.
- ADR 0008, the Phase 6 design note, and the Phase 6 report.
- Phase 7 `cyber_software_renderer`: clipped all-octant Bresenham lines;
  fixed-point edge-function triangle fill with winding normalization, the
  clockwise top-left rule, and unbiased barycentric vertex colors; tile binning
  with one `cyber_jobs` job per tile, producing bytes independent of worker
  count and tile size; guard-band, degenerate, and non-finite rejection; and
  `RasterStats` with per-tile pixel-write counts.
- A 10,000-triangle jittered mesh covers 640x360 exactly once, with serial and
  parallel output byte-identical across tile sizes 16/64/128 and one/four
  workers.
- `samples/02_raster` shows the animated triangle mosaic at 60 Hz with a
  frame-time overlay, a `--frames` smoke run, and a `--capture` mode that proves
  two images of the same frame are byte-identical.
- ADR 0009, the Phase 7 design note, and the Phase 7 report.
- Phase 8 `cyber_software_renderer`: `DepthBuffer` (float reverse-Z, clear 0,
  greater-or-equal test), `ClipTriangleNearPlane` and homogeneous near-plane
  clipping, `DrawTriangles` (model/view/projection, viewport y flip, back-face
  culling, +/-16384-pixel guard band), perspective-correct color through
  `1 / w`, `pixelsRejected` depth statistics, and the `Camera`, `FlyCamera`, and
  `OrbitCamera` values and controllers.
- Full-frame clears now fill rows by doubling a pixel pattern, and the depth
  clear uses `memset` for the all-zero reverse-Z far value; the Phase 7 capture
  hash is unchanged.
- `samples/03_depth` renders a depth-tested rotating cube over a perspective
  checkerboard floor with orbit/fly cameras, holds 60.0 FPS at 1280x720 in
  release, and captures byte-identical fixed frames including an inside-the-cube
  near-clipping pose.
- ADR 0010, the Phase 8 design note, and the Phase 8 report.
- Phase 9 `cyber_software_renderer`: a move-only `Texture` with a box-filtered
  mip chain, repeat/clamp/mirrored-repeat addressing, and nearest, bilinear, and
  trilinear sampling; a CPU shader interface (`VertexShaderFn`/`PixelShaderFn`
  over `VertexInput` and `Varyings`); `DrawShadedTriangles` and
  `RasterizeShadedTriangles` with perspective-correct varying interpolation and
  near-plane clipping; Lambert and Blinn-Phong lighting with directional and
  point lights; PPM (P6/P5/P3) and TGA (24/32-bit, both origins) readers that
  decode sRGB to linear; an OBJ reader with fan triangulation and negative
  indices; and a golden-image harness with per-channel tolerance and a
  difference image.
- Five committed 640x360 golden scenes (`checker_cube`, `lambert_cube`,
  `blinn_cube`, `uv_gradient`, `depth_order`) compared in every preset, plus a
  determinism check that renders twice byte-identically.
- `samples/04_textured` renders a checker-textured, lit cube with a Blinn-Phong
  highlight and captures byte-identical fixed frames. Its `--supersample` flag
  renders `n` samples per axis and box-filters the result in linear space, which
  antialiases the silhouette until the rasterizer has multisample coverage; the
  window lowers the factor to stay within 2560x1440.
- CI now runs AddressSanitizer on Linux through the `ci-linux-asan` preset.
- ADR 0011, ADR 0012 (supersampling until multisample coverage exists), the
  Phase 9 design note, `docs/testing-golden-images.md`, and the Phase 9 report.
- Phase 10 `cyber_rhi`: the GPU object model. Eleven generation-counted handle
  types; a `Format` enum with depth, sRGB, block-compressed, and integer
  classification; `BackendKind`, `AdapterInfo`, `Limits`, `Feature`, and
  `Capabilities`; the boundary conventions (+Y up, reverse-Z in 0..1, top-left
  texture origin) with a per-backend mapping table; a function-pointer backend
  boundary (`Backend`, `DeviceInterface`); a move-only `Device` with one queue
  per class, monotonic 64-bit timeline fences, and `WaitIdle`; and the null
  backend that validates every call.
- ADR 0013 (RHI object model), the Phase 10 design note, the D3D12/Vulkan/WebGPU
  comparison notes, and the Phase 10 report.

### Notes

- The public API, renderer, and tools are not implemented yet. Planned modules
  intentionally have folders and contracts but no CMake targets.
- `cyber_rhi` implements its object model only: resource, pipeline, and
  command-list creation arrive in Phases 11 to 13.
- The project test runner is a developer tool; it is not installed and is never
  linked into a shipping target.
- `Transform` is an authoring record. Non-uniform TRS is not closed under
  composition, so hierarchies compose `ToMatrix()` values and invert through
  `TryInverse(Mat4&)`.
