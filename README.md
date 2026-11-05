# CyberGraphics

CyberGraphics is Cyber Engine's own renderer: one C++ codebase from math types
up to the GPU backends, with no third-party rendering libraries. The only
external dependencies are the C++ standard library, operating system headers,
and the graphics driver APIs hidden behind the RHI.

This repository is being built phase by phase from the 44-phase CyberGraphics
roadmap. The repository, build foundation, `cyber_core` primitives,
`cyber_math`, `cyber_memory`, and the `cyber_platform`/`cyber_jobs` services are
in place, and every module test runs through the project's own test runner.

## Status

- Structure and module contracts: scaffolded.
- Build system: CMake 3.28+, C++20, Debug/Release/Shipping presets.
- Modules: 16 defined, 10 active (`cyber_core`, `cyber_math`, `cyber_memory`,
  `cyber_platform`, `cyber_perf`, `cyber_jobs`, `cyber_rhi`, `cyber_shaders`,
  `cyber_renderer`, `cyber_software_renderer`), 6 planned.
- CI: Windows MSVC and Linux clang jobs, plus structure and dependency checks.
- Phase 1: complete. Local builds and tests pass in every preset, structure and
  dependency checks pass, and the Windows + Linux CI jobs are green.
- Phase 2: complete. `cyber_core` provides fixed-width types, build
  configuration, `Result`, assertions, a bounded logger, xxHash64, and the
  project test runner in `tools/test`.
- Phase 3: complete. `cyber_math` provides vectors, matrices, quaternions, TRS
  authoring, reverse-Z view/projection matrices, geometry, linear/sRGB color,
  binary16 storage, and an SSE2 `Vec4` path. All 17 tests pass in every preset,
  both consumers pass, and the Windows + Linux CI jobs are green.
- Phase 4: complete. `cyber_memory` provides the allocator family, tagged leak
  reports, containers, strings, spans, and generation-checked handles. All 20
  tests pass in every local preset, both consumers pass, and the Windows + Linux
  CI jobs are green.
- Phase 5: complete. `cyber_platform` provides renderer-agnostic windows,
  input, native clocks, UTF-8 file I/O, memory mapping, a polling file watcher,
  native threads, synchronization wrappers, and dynamic-library loading.
  `cyber_perf` provides the profiler seam, and `cyber_jobs` provides the fixed
  worker pool, work stealing, dependency counters, handles, `ParallelFor`, and
  deterministic shutdown. All 31 tests pass in every local preset, both
  consumers pass, the visible-window sample runs, and the Windows + Linux CI
  jobs are green.
- Phase 6: complete. `cyber_software_renderer` provides linear `RGBA8Unorm`
  and `RGBA32F` framebuffers, clipped fills, blits and conversion, sRGB present
  packing, deterministic PPM/TGA export, bitmap overlay text, and a 60 Hz frame
  pacer. `cyber_platform` gains the `Presenter` seam and high-resolution sleeps.
  The gradient sample runs at 60.0 FPS and its two fixed-frame captures are
  byte-identical. All 39 tests pass in every local preset, both consumers pass,
  and the Windows + Linux CI jobs are green.
- Phase 7: complete. `cyber_software_renderer` adds clipped Bresenham lines,
  fixed-point edge-function triangle fill with the top-left rule and barycentric
  vertex color, and tile-parallel rasterization through `cyber_jobs`. A
  10,000-triangle mesh covers 640x360 exactly once, and serial and parallel
  output are byte-identical across tile sizes and worker counts. The raster
  sample holds 60.0 FPS in release and its two fixed-frame captures are
  byte-identical. All 41 tests pass in every local preset, both package
  consumers pass, and the Windows + Linux CI jobs are green.
- Phase 8: implementation complete. `cyber_software_renderer` adds the full
  vertex path (model/view/projection, homogeneous near-plane clipping,
  back-face culling, reverse-Z depth, perspective-correct color), the orbit and
  fly cameras, and `samples/03_depth`. The cube sample holds 60.0 FPS at
  1280x720 in release and both fixed-frame captures are byte-identical; the
  Phase 7 capture hash is unchanged. All 44 tests pass in every local preset,
  both package consumers pass, and the Windows + Linux CI jobs are green.
- Phase 9: implementation complete locally. `cyber_software_renderer` adds
  mipmapped textures with repeat/clamp/mirrored addressing and
  nearest/bilinear/trilinear sampling; a CPU shader interface with vertex and
  pixel stages over perspective-correct varyings; the shaded pipeline with
  near-plane clipping; Lambert and Blinn-Phong lighting; PPM, TGA, and OBJ
  readers; a golden-image harness with tolerance and difference images; five
  committed 640x360 golden scenes; and `samples/04_textured`. All 51 tests pass
  in debug, release, shipping, and ASan, both package consumers pass, every
  check is green, and the textured capture is byte-identical across two writes.
  The textured sample antialiases by supersampling (`--supersample`, up to 4
  samples per axis, resolved in linear space) because the rasterizer still takes
  one center sample per pixel.
  The Windows, Linux UBSan, Linux ASan, formatting, and structure CI jobs are
  green.
- Phase 10: the RHI object model. `cyber_rhi` becomes a built module with
eleven generation-counted handle types, a format enum with classification, a
capabilities value (adapter, limits, features), the boundary conventions (+Y
up, reverse-Z 0..1, top-left texture origin) with a per-backend mapping table,
a function-pointer backend boundary, and a move-only `Device` with one queue per  class and monotonic timeline fences. The null backend validates every call. 58
tests pass in debug; the design is ADR 0013.
- Phase 11: RHI resources. `cyber_rhi` gains buffers, textures, texture views,
  samplers, a numeric format table, and one resource-state vocabulary with
  validated transitions. The null backend validates every descriptor, tracks
  state per `(mip, layer)`, keeps real host-visible bytes so a write round-trips,
  and retires a texture's views when the texture dies. 63 tests pass in every
  preset; the design is ADR 0014.
- Phase 12: pipelines and the binding model. `cyber_rhi` adds shader modules,
  four binding slots (per frame, view, material, draw), group layouts and groups,
  pipeline layouts with push constants, graphics and compute pipeline
  descriptions, a hash-keyed pipeline cache, and the validation that rejects
  every impossible combination. 67 tests pass in every preset; the design is
  ADR 0015.
- Phase 13: command lists, synchronization, and the swapchain. `cyber_rhi`
  adds recorded lists with a checked state machine, explicit render passes,
  buffers and textures copy and clear, push constants, debug labels, one-call
  submission with timeline waits and signals, a frame ring, a retirement queue,
  and swapchains with acquire, present, resize, and present modes. The null
  backend tracks state per list and per subresource, so a missing barrier is a
  record-time error that names the resource. 74 tests pass in every preset; the
  design is ADR 0016.
- Phase 14: the Direct3D 12 backend. `cyber_rhi` enumerates DXGI adapters with
  vendor, device, and memory, appends Microsoft's WARP adapter last so a machine
  without a GPU is still testable, and creates a real `ID3D12Device` with three
  command queues, timeline fences, buffers, textures, views, samplers, HLSL
  shaders, bind groups over descriptor tables, graphics and compute pipelines with
  `ID3D12PipelineLibrary` caches, command lists, submission, and a flip-model
  swapchain. The done-when line is a comparison in one process: the same scene —
  a colored triangle, then a textured cube — rendered by the D3D12 backend and by
  `cyber_software_renderer`, compared with a recorded tolerance. Both images are
  4x supersampled and box-filtered to 512x512 (ADR 0012), the triangle is exact,
  and the cube differs on 1089 of 262144 pixels, all on silhouette edges. The
  debug layer runs with GPU-based validation and any message fails a test. The
  backend compiles out entirely when `CYBER_RHI_ENABLE_D3D12` is off. 81 tests
  pass in every preset; the design is ADR 0017 and the evidence is the Phase 14
  report.
- Phase 15: GPU memory and debug tooling. `cyber_rhi` suballocates D3D12 heaps,
  validates RHI state and lifetime rules, names objects, emits native pass and
  point markers in Debug/Profile, reads back GPU timestamps after fences,
  reports named leaks, and preserves device-loss diagnostics. No PIX runtime or
  other third-party renderer dependency is linked. The blue-clear marker sample
  is in `samples/05_d3d12_markers/`. Debug, Profile, Release, Shipping, and
  ASan suites pass 87/87. The Intel HD Graphics 530 RTV heap-kind bug is fixed;
  the sample now submits and presents, and the fresh RenderDoc and PIX captures
  confirm the nested `Clear Pass` and `Clear backbuffer` labels and the blue
  presented frame. See the [Phase 15 report](docs/reports/phase-15.md).
- Phase 16 is complete. `cyber_shaders` parses a small typed Cyber shader
  language, reports source locations, and emits HLSL; the D3D12 textured cube
  comparison uses the generated vertex and pixel shaders. Debug, Release, and
  ASan pass 88/88, and the repository-wide formatting gate passes. This is
  shader-tooling progress, not a photorealistic renderer. See the
  [Phase 16 report](docs/reports/phase-16.md).
- Phase 17 implementation and validation are complete locally. `cyber_shaders`
  derives reflection from typed source, builds bounded stable keyword
  permutations, caches compiler output, and reloads shaders asynchronously.
  `cyber_renderer` builds reflected RHI layouts and swaps pipelines at frame
  boundaries. Debug, Profile, Release, Shipping, and Debug-ASan pass 96/96; the
  D3D12 live-reload self-test applied edits in 48 ms and 84 ms and retained the
  previous pipeline after a syntax error. The master-roadmap checkbox awaits
  commit of this evidence. See the [Phase 17 report](docs/reports/phase-17.md).

See [engines/MODULES.md](engines/MODULES.md) for the module index and
[docs/architecture/overview.md](docs/architecture/overview.md) for the design.

## Layout

```text
engines/        Runtime modules, one contract per folder.
include/        Stable SDK ABI only (include/cybergraphics/).
tools/          Developer-only executables and libraries.
samples/        Runnable samples and a stub engine host.
tests/          Cross-module integration, golden, fuzz, stress, benchmarks.
docs/           Architecture, maths conventions, ADRs, design notes, reports.
cmake/          Shared CMake helpers and compiler policy.
scripts/        Bootstrap, build, test, format, and CI checks.
assets/         Small source assets; cooked output is generated.
third_party/    Empty by policy.
```

There is no root `src/`. Code belongs to the module that owns it.

## Build

Prerequisites on Windows:

- Visual Studio 2022 with the Desktop C++ workload (MSVC v143, Windows SDK).
- CMake 3.28 or newer and Ninja. The Visual Studio installation ships both;
  `scripts/bootstrap.ps1` locates them.

From a PowerShell prompt:

```powershell
.\scripts\build.ps1 -Preset debug -Test
```

`build.ps1` enters the Visual Studio developer environment, configures with
`CMakePresets.json`, builds, and optionally runs CTest. Run CTest alone with
`.\scripts\test.ps1 -Preset debug`. To use a system-wide CMake and Ninja instead,
put them on `PATH` and run:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

Linux uses the `ci-linux` preset with clang and UBSan.

## Using CyberGraphics from Cyber Engine

CyberGraphics can be consumed as a source subdirectory or as an installed CMake
package. Both modes expose the same targets:

- `cyber::graphics_api` — the stable product contract.
- `cyber::core` — core primitives.
- `cyber::math` - vectors, matrices, quaternions, projections, geometry, color,
  and half floats.
- `cyber::memory` - allocators, containers, strings, and generation handles.

Source mode:

```cmake
set(CYBER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CYBER_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(CYBER_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/CyberGraphics)

target_link_libraries(cyber_engine_renderer PRIVATE
    cyber::graphics_api
    cyber::core
    cyber::math
    cyber::memory
)
```

Package mode:

```powershell
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix C:/SDK/CyberGraphics
```

```cmake
find_package(CyberGraphics 0.1 CONFIG REQUIRED)
target_link_libraries(cyber_engine_renderer PRIVATE
    cyber::graphics_api
    cyber::core
    cyber::math
    cyber::memory
)
```

The package consumer in `tests/integration/package_consumer` and the source
consumer in `tests/integration/source_consumer` run in CI on Windows and Linux.
The full embedding contract, build options, ABI rules, and integration
checklist are in [docs/architecture/integration.md](docs/architecture/integration.md).

## Module law

1. Dependencies point downward in `order`; never upward.
2. A module may depend only on a module with a lower `order`, or on a declared
   contract such as `cybergraphics_api`.
3. No module includes another module's `src/`.
4. Public headers are self-contained.
5. `scripts/layer_check.py` and `scripts/check_structure.py` enforce the rules
   in CI.

Run them locally with:

```powershell
python scripts/check_structure.py
python scripts/layer_check.py
python scripts/check_format.py
```

## Documentation

- [Roadmap index](docs/ROADMAP.md)
- [CyberGraphics roadmap](docs/graphics/CYBERGRAPHICS_ROADMAP.md)
- [Build guide](docs/graphics/CYBERGRAPHICS_BUILD_GUIDE.md)
- [Requirements](docs/requirements.md)
- [Architecture overview](docs/architecture/overview.md)
- [Integration with Cyber Engine](docs/architecture/integration.md)
- [Layer rules](docs/architecture/layers.md)
- [Module contract](docs/architecture/module-contract.md)
- [Math conventions](docs/math-conventions.md)
- [Coding standard](docs/coding-standard.md)
- [Testing strategy](docs/testing/README.md)
- [Decisions (ADRs)](docs/adr/README.md)

## License

Proprietary. Copyright (c) 2026 CyberEngine. All rights reserved. See
[LICENSE](LICENSE).
