# Foundation hardening — integration-ready build

Date: 2026-10-20
Status: implemented, awaiting owner review

## Why this pass exists

Phase 1 proved that the repository builds and that the dependency law is
enforced. It did not prove that Cyber Engine can consume the result. The first
scaffold had no install rules, no exported package, no consumer test, and
warnings leaked through a public interface target. Those are integration risks,
not cosmetics: a host and renderer that disagree on the MSVC runtime, target
names, or headers fail at load time.

This pass closes those gaps before Phase 2 adds real code to `cyber_core`.

## What it changes

1. **Install and export.** `cyber::core` and `cyber::graphics_api` are exported
   through `CyberGraphicsConfig.cmake`, with headers, license, and notices.
2. **Two integration modes.** Cyber Engine can use `add_subdirectory` or
   `find_package(CyberGraphics CONFIG)`.
3. **Runtime pinning.** `CMAKE_MSVC_RUNTIME_LIBRARY` defaults to the dynamic
   runtime, with an explicit static option.
4. **Single-source version.** `project(VERSION)` generates the version header
   used by `cyber_core`.
5. **Warning isolation.** Warnings are applied by a function; they are no longer
   a link dependency and do not enter the SDK.
6. **Collision-proof tests.** Test executables are prefixed; duplicate CTest
   names are rejected at configure time.
7. **CI gates.** Formatting is checked, actions are SHA-pinned, Dependabot
   tracks updates, and both Windows and Linux install the package and build a
   consumer against it.
8. **Security and integration docs.** `SECURITY.md`, ADR 0003, and the
   integration guide define the boundary before the ABI exists.

## What it deliberately leaves out

- The stable C ABI itself. The package exports the location and the target, not
  a frozen renderer interface.
- Plugin binaries. `CYBER_BUILD_PLUGINS` is reserved; products arrive with their
  roadmap phases.
- Branch protection. It is a GitHub repository setting, not a file; the required
  checks are documented in `CONTRIBUTING.md`.
- A release workflow, signing, and binary artifacts. Those belong to Phase 43.
- Sanitizer coverage of the package consumer. It is a build/link test, not a
  runtime feature test.

## Design

### Package shape

```text
include/cyber/core/version.h
include/cyber/core/version_generated.h
include/cybergraphics/...
lib/cyber_core.lib
lib/cmake/CyberGraphics/CyberGraphicsConfig.cmake
lib/cmake/CyberGraphics/CyberGraphicsTargets.cmake
share/CyberGraphics/LICENSE
share/CyberGraphics/THIRD_PARTY_NOTICES.md
```

`EXPORT_NAME` maps internal targets to stable names. The installed target is
`cyber::core`, not `cyber::cyber_core`; the same is true for
`cyber::graphics_api`.

### Why warnings are a function

An interface target that carries `/W4` and `/WX` propagates into every consumer
and must be exported. That is wrong for a library: warning policy is private to
the build. `cyber_apply_warnings(target)` applies `PRIVATE` flags, so the
consumer cannot inherit them and the export set stays clean.

### Why the runtime is explicit

A plugin built with `/MT` and a host built with `/MD` can appear to link and then
fail at runtime because they have different CRT state and allocation rules.
The default is `/MD`; `CYBER_MSVC_STATIC_RUNTIME=ON` selects `/MT` for a host
that uses a static runtime. The integration guide makes this a checked
requirement.

### Why the version is generated

`project(VERSION 0.1.0)` is the only place the project version is written.
`cmake/version_generated.h.in` becomes `cyber/core/version_generated.h` in the
build tree and is installed with the headers. `cyber_core` no longer hard-codes
a second copy.

### Consumer proof

`tests/integration/package_consumer` is a separate CMake project. CI installs
CyberGraphics to a prefix, configures the consumer with
`CMAKE_PREFIX_PATH`, builds it, and runs it. It links both exported targets and
includes the installed version header, so a missing header, wrong include path,
or broken export fails the build.

`tests/integration/source_consumer` configures the same check in
`add_subdirectory` mode with tests, samples, and install disabled. Both
consumers use the same source file, so the two integration modes cannot drift.

## Risks

- **Package drift:** a new module can be added without being installed. The
  consumer test only covers `core` and `graphics_api`; update the test when a
  new public module is exported.
- **Runtime mismatch:** the option cannot detect a host built with the other
  runtime. The integration checklist and the version manifest must state it.
- **Formatting churn:** enabling a format gate on an existing tree can produce a
  large diff. The tree is small now; doing it before Phase 2 avoids a bigger
  one later.
- **Action pinning:** SHA pins are secure but need Dependabot review. A stale
  pin is not automatically wrong.
- **Install path assumptions:** `find_package` needs `CMAKE_PREFIX_PATH` or a
  standard prefix. The integration guide states this.

## Test plan

1. Clean configure/build/test in `debug`, `release`, `shipping`, and
   `debug-asan`.
2. `python scripts/check_structure.py`, `python scripts/layer_check.py`, and
   `python scripts/check_format.py`.
3. `cmake --install build/debug --prefix build/install` on Windows.
4. Configure, build, and run `tests/integration/package_consumer` against that
   prefix.
5. Configure, build, and run `tests/integration/source_consumer` against the
   source tree.
6. Push and confirm all CI jobs: structure, formatting, Windows, Linux.
7. Confirm Windows and Linux package and source consumer steps pass in CI.

## Sources

- CMake documentation: `install(EXPORT)`, `configure_package_config_file`,
  `CMAKE_MSVC_RUNTIME_LIBRARY`, `EXPORT_NAME`.
- Microsoft C++ documentation: `/MD`, `/MT`, `/EH`, `/GR`.
- GitHub Actions documentation: SHA pinning, Dependabot for actions.
- Cyber Engine integration requirements from the owner.
