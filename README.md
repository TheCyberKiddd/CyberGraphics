# CyberGraphics

CyberGraphics is Cyber Engine's own renderer: one C++ codebase from math types
up to the GPU backends, with no third-party rendering libraries. The only
external dependencies are the C++ standard library, operating system headers,
and the graphics driver APIs hidden behind the RHI.

This repository is being built phase by phase from the 44-phase CyberGraphics
roadmap. The current milestone is the repository and build foundation; the
module tree, contracts, and checks are in place, and `cyber_core` is the
reference module used to prove the build.

## Status

- Structure and module contracts: scaffolded.
- Build system: CMake 3.28+, C++20, Debug/Release/Shipping presets.
- Modules: 16 defined, 1 scaffolded (`cyber_core`), 15 planned.
- CI: Windows MSVC and Linux clang jobs, plus structure and dependency checks.
- Phase 1: local builds and tests pass in every preset; remote CI is pending
  the first push.
- Next: owner review of the Phase 1 report, then Phase 2 (`cyber_core`
  primitives and the test runner).

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
`CMakePresets.json`, builds, and optionally runs CTest. To use a system-wide
CMake and Ninja instead, put them on `PATH` and run:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

Linux uses the `ci-linux` preset with clang and UBSan.

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
```

## Documentation

- [Requirements](docs/requirements.md)
- [Architecture overview](docs/architecture/overview.md)
- [Layer rules](docs/architecture/layers.md)
- [Module contract](docs/architecture/module-contract.md)
- [Math conventions](docs/math-conventions.md)
- [Coding standard](docs/coding-standard.md)
- [Testing strategy](docs/testing/README.md)
- [Decisions (ADRs)](docs/adr/README.md)

## License

Proprietary. Copyright (c) 2026 CyberEngine. All rights reserved. See
[LICENSE](LICENSE).
