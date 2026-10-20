# Phase 1 report: repository and build foundation

Date: 2026-10-20
Commits: `07c560f` — `chore: scaffold repository and Phase 1 foundation`
Machine: Intel Core i5-6400, 16 GB RAM, Intel HD Graphics 530 (driver
31.0.101.2125), Windows 10 Pro 10.0.19045, MSVC 19.44.35225.0, CMake 3.31.6,
Ninja.

## What was built

- The complete repository layout: `engines/`, `include/`, `tools/`, `samples/`,
  `tests/`, `docs/`, `cmake/`, `scripts/`, `assets/`, `third_party/`, and
  `.github/`.
- Sixteen module folders, each with `README.md`, `docs/MODULE.md`, and
  `include/`, `src/`, and `tests/` directories.
- `engines/modules.json` and the generated `engines/MODULES.md` index.
- `scripts/scaffold_modules.py`, `scripts/layer_check.py`, and
  `scripts/check_structure.py`.
- CMake helpers: compiler options, warnings, platform macros, sanitizers,
  module/test helpers, and check targets.
- Root `CMakeLists.txt` and `CMakePresets.json` with `debug`, `release`,
  `shipping`, `debug-asan`, `ci-windows`, and `ci-linux`.
- The `cyber_core` reference module: `cyber/core/version.h`, `version.cpp`, and
  a `core_version` CTest executable.
- PowerShell `bootstrap.ps1`, `build.ps1`, `test.ps1`, and `format.ps1`.
- Windows and Linux CI workflow, CODEOWNERS, and a pull-request template.
- Project documents: README, LICENSE (CyberEngine), CHANGELOG, CONTRIBUTING,
  THIRD_PARTY_NOTICES, requirements, architecture, math conventions, coding
  standard, glossary, and the design/ADR/notes/reports/specs/testing/perf
  indexes.
- ADRs 0001 (module architecture) and 0002 (math and depth conventions).

## Evidence for done-when

### Structure and dependency law

```text
> python scripts/check_structure.py
Structure check passed: 16 modules, 1 contract(s).

> python scripts/layer_check.py
Layer check passed: 16 modules.
```

### Windows builds and tests

Each command was run from a clean build directory. Warnings are errors.

| Preset | Build | Test | Time |
|---|---|---|---|
| `debug` | Passed | `core_version` 1/1 | 0.04 s |
| `release` | Passed | `core_version` 1/1 | 0.02 s |
| `shipping` | Passed | `core_version` 1/1 | 0.02 s |
| `debug-asan` | Passed | `core_version` 1/1 | 0.25 s |

Commands:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset debug -Clean -Test
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset release -Clean -Test
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset shipping -Clean -Test
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset debug-asan -Clean -Test
```

The first build produced MSVC D9025 overriding-option warnings because CMake's
default `/EHsc` conflicted with the module's `/EHs-c-`. `CompilerOptions.cmake`
now strips `/EHsc` from the default flags and each target chooses its exception
model; tests explicitly keep `/EHsc`. The clean rebuild is warning-free.

### Toolchain discovery

`bootstrap.ps1` selected Visual Studio 2022 Professional and its bundled CMake
3.31.6 and Ninja, even though VS 2026 Build Tools is also installed and neither
CMake nor Ninja is on the system `PATH`.

### CI

The repository is published at
`https://github.com/TheCyberKiddd/CyberGraphics` (private). The workflow ran on
the initial push and all three jobs passed:

| Job | Runner | Result |
|---|---|---|
| Structure and dependency law | `ubuntu-24.04` | Passed |
| Windows MSVC | `windows-2022` | Passed |
| Linux clang + UBSan | `ubuntu-24.04` | Passed |

Run: `39402715117`, conclusion `success`, 32 seconds.

```powershell
gh run list --limit 5
gh run view 39402715117 --json status,conclusion,jobs
```

## Known gaps

1. **Linux is not verified locally.** WSL Ubuntu is installed but has no CMake,
   clang, or Ninja. The Linux CI job is green, which is the required evidence.
2. **No clean-machine test on a second physical machine.** CI is the closest
   substitute; a fresh Windows VM run is still worth doing before Phase 2.
3. **`clang-format` is not installed locally.** `format.ps1` is written but has
   not been exercised. The VS 2022 installation lacks the LLVM component.
4. **The stable ABI is a reserved directory, not a header.** This is intentional
   until Phase 25.
5. **Planned modules have no targets.** Also intentional; the structure check
   enforces it.
6. **The repository identity is repo-local.** `user.email` is set to
   `thecyberkiddd@gmail.com` for this repository. The global Git value remains
   `thecyberkiddd@gmail.com@gmail.com` and was not modified.

## Sources used

- CMake presets and MSVC flag documentation.
- Microsoft C++ `/EH`, `/GR`, `/W4`, and AddressSanitizer documentation.
- GitHub Actions `windows-2022` and `ubuntu-24.04` image documentation.
- glTF 2.0 specification for the coordinate-system wording.
- The owner's roadmap and build guide.

## What the next phase needs from this one

- The `cyber_core` target, public include root, and test helper pattern.
- The stable `cyber::graphics_api` interface target, currently header-only.
- The presets and scripts, unchanged.
- The dependency manifest and checks, so new core headers stay dependency-free.
- The math conventions and ADR 0002, so Phase 3 can implement projections against
  fixed rules.

## Review decision requested

Phase 1 is complete: local builds pass in every preset, structure and dependency
checks pass, and remote CI is green. Before Phase 2 starts, the owner should
confirm:

1. VS 2022 is the pinned local toolchain, matching `windows-2022` CI.
2. Phase 2 starts with `cyber_core` primitives and the project test runner.
3. The repository stays private and standalone until the submodule decision.
