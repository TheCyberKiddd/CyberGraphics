# Phase 1 report: repository and build foundation

Date: 2026-10-20
Commits: none yet; the working tree is uncommitted.
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

The workflow defines three jobs: structure checks, Windows MSVC build/test, and
Ubuntu 24.04 clang + UBSan build/test. The workflow has not run remotely because
the repository has not been created or pushed.

## Known gaps

1. **Remote CI is not green yet.** This is the remaining Phase 1 gate. The
   repository is not initialized, and no remote exists.
2. **Linux build is unverified locally.** WSL Ubuntu is installed but has no
   CMake, clang, or Ninja, and no packages were installed without permission.
3. **No git history.** No commit has been made, so the report cannot name a
   commit range. The working tree is the evidence.
4. **No clean-machine test.** The build has only run on the development machine.
5. **`clang-format` is not installed locally.** `format.ps1` is written but has
   not been exercised. The VS 2022 installation lacks the LLVM component.
6. **The stable ABI is a reserved directory, not a header.** This is intentional
   until Phase 25.
7. **Planned modules have no targets.** Also intentional; the structure check
   enforces it.

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

Phase 1 is locally complete except for remote CI. Before Phase 2 starts, the
owner should confirm:

1. Initialize the git repository and push it to a private remote so CI can run.
2. Confirm the git author email. The current global value is
   `thecyberkiddd@gmail.com@gmail.com`; the intended value is
   `thecyberkiddd@gmail.com`. The malformed value must not be used in commits.
3. Confirm VS 2022 as the pinned local toolchain, matching `windows-2022` CI.
4. Confirm that Phase 2 starts with `cyber_core` primitives and the test runner.
