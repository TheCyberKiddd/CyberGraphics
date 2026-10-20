# Foundation hardening report: integration-ready build

Date: 2026-10-20
Commits: recorded after the hardening commit
Machine: Intel Core i5-6400, 16 GB RAM, Intel HD Graphics 530, Windows 10 Pro
10.0.19045, MSVC 19.44.35225.0, CMake 3.31.6, Ninja.

## What was built

- Install rules for `cyber_core` and `cyber_graphics_api`, including headers,
  generated version header, license, and notices.
- `CyberGraphicsConfig.cmake` and `CyberGraphicsTargets.cmake` with stable
  `cyber::core` and `cyber::graphics_api` names.
- Single-source version generation from `project(VERSION)`.
- Explicit MSVC runtime selection (`/MD` by default, `/MT` option).
- Warning policy as a function, not a public interface target.
- Prefixed test targets and duplicate CTest-name rejection.
- `scripts/verify_package.ps1`, `tests/integration/package_consumer`, and
  `tests/integration/source_consumer`.
- `scripts/check_format.py` and a formatting CI job.
- SHA-pinned Actions, Dependabot, `SECURITY.md`, ADR 0003, and the integration
  guide.
- Build options for submodule consumption: `CYBER_BUILD_TESTS`,
  `CYBER_BUILD_SAMPLES`, `CYBER_BUILD_PLUGINS`, `CYBER_INSTALL`.

## Local evidence

### Builds and tests

Each preset was configured from a clean build directory. Warnings are errors.

| Preset | Build | Test | Time |
|---|---|---|---|
| `debug` | Passed | `core_version` 1/1 | 0.05 s |
| `release` | Passed | `core_version` 1/1 | 0.04 s |
| `shipping` | Passed | `core_version` 1/1 | 1.74 s |
| `debug-asan` | Passed | `core_version` 1/1 | 0.24 s |

The compile line for the runtime module contains `/MDd`, `/EHs-c-`, and `/GR-`.
The test target contains `/MDd` and `/EHsc`; no warning override is emitted.

A separate `build/static-runtime` configuration with
`CYBER_MSVC_STATIC_RUNTIME=ON` built and passed `core_version`, confirming the
`/MT` path is selectable. Package verification used the default `/MD` runtime.

### Installed package

```text
build/install-debug/
  include/cyber/core/version.h
  include/cyber/core/version_generated.h
  lib/cyber_core.lib
  lib/cmake/CyberGraphics/CyberGraphicsConfig.cmake
  lib/cmake/CyberGraphics/CyberGraphicsConfigVersion.cmake
  lib/cmake/CyberGraphics/CyberGraphicsTargets.cmake
  share/CyberGraphics/LICENSE
  share/CyberGraphics/THIRD_PARTY_NOTICES.md
```

The `include/cybergraphics/` headers are installed when they exist; the
directory has no public ABI headers until Phase 25.

`scripts/verify_package.ps1 -Preset debug` installed the package, configured
`tests/integration/package_consumer` against it, built `consumer.exe`, and ran
it successfully (exit 0). The same check passed for the `release` preset with a
Release consumer, so the debug and release runtime paths are both covered.

`tests/integration/source_consumer` was configured with
`CYBER_BUILD_TESTS=OFF`, `CYBER_BUILD_SAMPLES=OFF`, and `CYBER_INSTALL=OFF`,
then built and ran `source_consumer.exe` successfully. It uses the same source
file as the package consumer.

### Formatting

```text
> python scripts/check_format.py
Formatting check passed: 4 files.
```

`format.ps1` now prefers the VS 2022 LLVM installation, matching the pinned
toolchain, instead of selecting the newest VS installation.

### Structure and dependency law

```text
> python scripts/check_structure.py
Structure check passed: 16 modules, 1 contract(s).

> python scripts/layer_check.py
Layer check passed: 16 modules.
```

## CI evidence

Pending the hardening push. The workflow now runs:

- structure and dependency law;
- formatting with `clang-format`;
- Windows MSVC build, test, install, package consumer, and source consumer;
- Linux clang + UBSan build/test, a non-sanitized release package build,
  install, package consumer, and source consumer.

## Known gaps

1. **The consumer covers only `core` and `graphics_api`.** New exported modules
   must extend it.
2. **Runtime mismatch is not detected automatically.** The integration guide
   requires the host and plugin to use the same `/MD` or `/MT`; a future version
   manifest should record it.
3. **Branch protection is a repository setting.** The required check names are
   documented in `CONTRIBUTING.md`; they are not configured by a file.
4. **The format gate covers C++ only.** CMake, YAML, Markdown, and PowerShell
   formatting are not gated yet.
5. **No binary release.** Install is local; signing, symbols, and artifact
   publishing belong to Phase 43.
6. **Actions are SHA-pinned.** Dependabot proposes updates; a human must review
   that the new SHA is the intended release.
7. **Package verification uses a non-sanitized build on Linux.** UBSan and ASan
   runtimes are build-local and are not part of the installed contract. The
   sanitized build is still tested separately.

## What Phase 2 needs from this pass

- `cyber::core` exported and installed.
- A consumer project to extend when new public headers arrive.
- The runtime and version decisions fixed.
- The integration guide as the contract for the future Cyber Engine host.

## Review decision requested

Confirm the integration and packaging approach, then start Phase 2:
`cyber_core` primitives and the project test runner.
