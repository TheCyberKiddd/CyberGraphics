# Phase 1 — Repository and build foundation

Date: 2026-10-20
Status: implemented, awaiting owner review

## What this phase builds

The skeleton every later phase depends on:

- the top-level directory layout (`engines/`, `include/`, `tools/`, `samples/`,
  `tests/`, `docs/`, `cmake/`, `scripts/`, `assets/`, `third_party/`);
- sixteen module folders, each with a README, a written contract, and public,
  private, and test directories;
- `engines/modules.json` as the single source of truth for layer, order,
  dependencies, forbidden dependencies, invariants, non-goals, tests, budgets,
  and roadmap phases;
- `engines/MODULES.md`, generated from that manifest;
- the dependency-law checker and the structure checker;
- a CMake 3.28+ build with Debug, Release, Shipping, ASan, and CI presets;
- compiler policy: C++20, warnings as errors, exceptions and RTTI disabled in
  runtime modules, sanitizer switches;
- a reference module, `cyber_core`, with a version header, a compiled source
  file, and a CTest executable;
- the stable product API location, `include/cybergraphics/`, reserved for the
  C ABI;
- Windows and Linux CI jobs plus the local bootstrap, build, test, and format
  scripts;
- project documentation: architecture, layers, module contract, threading,
  error model, ABI policy, math conventions, coding standard, glossary,
  requirements, and the design/ADR/notes/reports/specs/testing/perf indexes.

## What this phase deliberately leaves out

- The public C ABI. It is documented by policy but not declared until Phase 25,
  when the renderer design is stable enough to freeze.
- `cyber_math`, `cyber_memory`, and the rest of the foundation modules. They get
  CMake targets when their phases start, not before.
- The test runner. Phase 2 introduces it; Phase 1 uses CTest with a plain
  executable.
- GPU code, window creation, shader compilation, asset cooking, and samples.
- A package or install step. Shipping and packaging arrive with the product.
- A remote repository and green remote CI. The workflow is written, but the
  repository has not been pushed; local evidence is recorded in the phase
  report.

## Interfaces it touches

- `engines/modules.json` — the module manifest.
- `engines/<module>/docs/MODULE.md` — the per-module contract.
- `cmake/CyberModule.cmake` — `cyber_add_module` and `cyber_add_test`.
- `CMakePresets.json` — the names every script and CI job uses.
- `include/cybergraphics/` — the reserved stable ABI root.

## Design

### Modules are the unit of ownership

There is no root `src/`. Every file belongs to the module that owns it. A module
is independently buildable, has one responsibility, and declares its
dependencies. Products (`cyber_graphics`, `cyber_software_renderer`) are
modules too, but they implement the stable contract instead of being consumed
by other modules.

The full module tree is created now for navigation. Only `cyber_core` has a
CMake target. This is deliberate: an empty target invites accidental
dependencies, and a planned module should not pretend to be buildable.

### One-way dependency law

`order` is the enforceable rank. A module may depend only on a module with a
lower order, or on a declared contract such as `cybergraphics_api`. Layers are
documentation; orders are the rule. Gaps of ten leave room for insertion.

Two scripts enforce the law:

- `layer_check.py` validates the manifest, rejects unknown and upward
  dependencies, detects cycles, compares CMake declarations to the manifest,
  and scans includes for undeclared cross-module use.
- `check_structure.py` verifies required paths and contracts, ensures only
  active modules have CMake targets, keeps `third_party/` empty, and invokes
  the layer check.

Both run in CI before any compiler does. A structural violation is cheaper to
fix than a build failure caused by an accidental dependency.

### The build is a product, not a side effect

Presets are the interface:

| Preset | Purpose |
|---|---|
| `debug` | Default developer build, warnings as errors. |
| `release` | Optimized build with assertions still available. |
| `shipping` | Release plus LTO and `CYBER_SHIPPING`. |
| `debug-asan` | MSVC AddressSanitizer. |
| `ci-windows` | CI Windows build. |
| `ci-linux` | Clang with UBSan. |

`scripts/bootstrap.ps1` enters the Visual Studio developer environment, prefers
VS 2022 for parity with `windows-2022`, and adds the VS-bundled CMake and Ninja
when they are not on `PATH`. The build, test, and format scripts use it, so a
fresh terminal does not need a preconfigured shell.

### Compiler policy is centralized

`CompilerOptions.cmake` defines `CYBER_DEBUG`, `CYBER_RELEASE`, `CYBER_SHIPPING`,
and the platform macro. Runtime modules get `/EHs-c- /GR-` on MSVC and
`-fno-exceptions -fno-rtti` elsewhere. CMake's default `/EHsc` is removed so the
target choice does not produce an overriding-option warning. Tests keep
exceptions enabled because the standard library and test helpers assume them;
tests are not shipped.

Warnings are a separate interface target (`cyber::warnings`) so every target
gets the same policy and CI can promote warnings to errors.

### The stable ABI is a location before it is a file

`include/cybergraphics/` is the only public SDK root. Module headers are
source-compatible for tools built from the same revision, not binary-stable.
Keeping the two separate now prevents a module's C++ types from leaking into
the plugin contract later.

### Documentation is part of the scaffold

The phase loop requires reading notes, a design note, an ADR when a decision is
expensive to reverse, and a report with evidence. The templates are in place
before the first real phase so they are used, not retrofitted.

## Alternatives considered

| Alternative | Why not |
|---|---|
| One root `src/` and one library | Fast to start, impossible to enforce boundaries; every later split is a rewrite. |
| One CMake target per planned module now | Empty targets create link-order noise and false dependencies; contracts should precede targets. |
| A third-party build dependency manager | The project rule is stdlib plus OS headers; CMake and Ninja are already present with Visual Studio. |
| Visual Studio solutions as the source of truth | Not reproducible on Linux CI and hostile to review. CMake presets are portable. |
| Header-only modules | Compile-time cost, hidden coupling, and no place for private state. Libraries with explicit public headers are cheaper at scale. |
| Freeze the C ABI now | The renderer design is not settled; a premature ABI is a permanent mistake. |

## Risks

- **Over-scaffolding:** sixteen folders can look like sixteen implemented
  modules. Mitigation: status in the manifest, a generated index, and a
  structure check that forbids CMake targets for planned modules.
- **Toolchain drift:** VS 2022 and VS 2026 both exist locally. Mitigation:
  bootstrap prefers VS 2022; the CI image is pinned to `windows-2022`.
- **Unverified Linux job:** WSL is present but has no CMake or clang, so the
  Linux job has not run locally. Mitigation: the job is small and uses the
  documented presets; it must be green before Phase 1 is marked complete.
- **Remote CI not yet green:** the repository has not been created or pushed.
  Mitigation: local evidence is recorded; remote CI is the remaining gate.
- **Script execution policy:** PowerShell may block unsigned scripts.
  Mitigation: documented invocation with `-ExecutionPolicy Bypass` when needed.
- **Manifest drift:** `MODULES.md` and contracts are generated. Mitigation:
  regenerate with `scaffold_modules.py`; the structure check verifies the
  generated index and contracts exist.

## Test plan

Phase 1 is proven by:

1. `python scripts/check_structure.py` — 16 modules, 1 active contract.
2. `python scripts/layer_check.py` — 16 modules, no violations.
3. Clean configure and build in `debug`, `release`, `shipping`, and
   `debug-asan` with warnings as errors.
4. `ctest` in each preset — the `core_version` test passes.
5. Remote CI on `windows-2022` and `ubuntu-24.04` — the remaining gate.
6. A clean-machine run: clone, bootstrap, build, test, with no manual PATH
   edits.

## Sources

- CMake documentation: presets v6, `CMAKE_CXX_STANDARD`, MSVC runtime flags.
- Microsoft C++ documentation: `/EH`, `/GR`, `/W4`, AddressSanitizer.
- GitHub Actions documentation: `windows-2022`, `ubuntu-24.04`,
  `ilammy/msvc-dev-cmd`.
- glTF 2.0 specification (coordinate system, units), for the math note.
- The CyberGraphics roadmap and build guide supplied by the owner.
