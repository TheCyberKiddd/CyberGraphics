# 0003 — Integration and packaging contract

- **Status:** accepted
- **Date:** 2026-10-20
- **Deciders:** CyberEngine (owner)
- **Related:** Phase 1, [integration guide](../architecture/integration.md),
  [ABI policy](../architecture/abi-policy.md)

## Context

CyberGraphics will become a Cyber Engine submodule. The engine must be able to
consume it without copying files, guessing include paths, or sharing a build
directory. It also needs a later binary distribution path for the renderer
plugins. Both paths must expose the same targets and the same stable ABI,
otherwise the software renderer and the GPU renderer will drift apart.

The first scaffold had no install rules, exported targets, package config, or
consumer test. That is acceptable for a standalone prototype but not for an
engine dependency.

## Decision

1. **Support two integration modes.** Cyber Engine may use
   `add_subdirectory` with tests/examples/install disabled, or consume an
   installed `CyberGraphicsConfig.cmake` package.
2. **Expose stable target names.** `cyber::graphics_api`, `cyber::core`, and
   later modules use the same names in both modes. `EXPORT_NAME` maps the
   internal `cyber_core` target to `cyber::core`.
3. **Do not export build policy.** Compiler warnings are applied by a function,
   not linked as an interface target. Consumers do not inherit `/W4`, `/WX`, or
   `-Werror`.
4. **Pin the MSVC runtime.** The default is `/MD` (`MultiThreadedDLL`), with an
   option for `/MT`; the host and plugin must use the same runtime.
5. **Single-source the version.** CMake's `project(VERSION)` generates
   `cyber/core/version_generated.h`; modules do not hard-code a version.
6. **Test the package path in CI.** Windows and Linux install the project,
   configure `tests/integration/package_consumer`, build it, and run it.
7. **Keep the C ABI separate.** The package installs
   `include/cybergraphics/`, but the ABI is not implemented until Phase 25.
8. **Do not install internal build files.** Test executables, warning flags,
   `CMakeLists.txt`, `.gitkeep`, and development scripts stay out of the SDK.

## Consequences

### Positive

- Cyber Engine can adopt the renderer with a two-line CMake change.
- Package breakage is caught by CI before the engine sees it.
- Warning policy cannot leak into the host build.
- The runtime choice is explicit, which prevents a class of plugin crashes.
- The version has one source of truth.

### Negative

- Every new public module must be added to the install/export set.
- The package consumer is a second build in CI and adds time.
- `/MT` and `/MD` remain a host/plugin compatibility requirement that cannot be
  solved by headers alone.

### Neutral

- The package is static-library based for now; plugin products will be shared
  libraries later.
- `CYBER_INSTALL=OFF` is the normal setting for submodule use.

## Alternatives considered

| Alternative | Why not |
|---|---|
| Copy headers and libraries into Cyber Engine | Guarantees drift and makes upgrades painful. |
| Only support `add_subdirectory` | Forces the engine to build the renderer source in its own build graph; no binary distribution. |
| Only support `find_package` | Makes local development and debugging across both repositories awkward. |
| Export the warning interface target | Leaks project policy into consumers and complicates exports. |
| Leave the MSVC runtime to the default | A host/plugin mismatch is a runtime failure that is hard to diagnose. |
| Hard-code the version in `cyber_core` | Two sources of truth; the project version and the module version can disagree. |

## Follow-up

- Phase 2 adds the `cyber_core` primitives to the package consumer.
- Phase 14 adds the first GPU plugin target and a real plugin load test.
- Phase 25 implements the C ABI and the version negotiation.
- Phase 43 decides the final license, distribution, and symbol policy.
