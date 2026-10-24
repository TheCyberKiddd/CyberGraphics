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
- Modules: 16 defined, 7 active (`cyber_core`, `cyber_math`, `cyber_memory`,
  `cyber_platform`, `cyber_perf`, `cyber_jobs`, `cyber_software_renderer`), 9
  planned.
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
  byte-identical. All 41 local tests pass in every preset and both package
  consumers pass.
- Next: Phase 8 (depth, clipping, and perspective).

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
