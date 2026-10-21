# Integrating CyberGraphics into Cyber Engine

CyberGraphics must be usable in two ways without changing its module APIs:

1. **Source integration** — Cyber Engine adds the repository as a submodule and
   calls `add_subdirectory`.
2. **Package integration** — Cyber Engine installs CyberGraphics and consumes
   it with `find_package(CyberGraphics CONFIG)`.

Both modes expose the same targets and the same stable C ABI. This document is
the contract between the renderer repository and the future engine repository.

## Build options

A parent project sets these before `add_subdirectory`:

| Option | Default | Meaning |
|---|---|---|
| `CYBER_BUILD_TESTS` | `ON` | Build unit and integration tests. |
| `CYBER_BUILD_SAMPLES` | `ON` | Build runnable samples. |
| `CYBER_BUILD_PLUGINS` | `OFF` | Build renderer products as loadable plugins. |
| `CYBER_INSTALL` | `ON` | Generate install and CMake package rules. |
| `CYBER_WARNINGS_AS_ERRORS` | `ON` | Promote warnings to errors. |
| `CYBER_MSVC_STATIC_RUNTIME` | `OFF` | Use `/MT` instead of `/MD`. Keep the host and plugin consistent. |
| `CYBER_NO_EXCEPTIONS` | `ON` | Runtime modules compile without C++ exceptions. |
| `CYBER_NO_RTTI` | `ON` | Runtime modules compile without RTTI. |

`CYBER_IS_TOP_LEVEL` is set by the build when CyberGraphics is the top-level
project. It is informational; options remain explicit.

### Source integration

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

The engine still owns its application, job system, asset database, and window.
CyberGraphics owns the GPU device, renderer, and renderer-side memory.

### Package integration

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

The install tree contains:

```text
include/cyber/core/...          core module headers
include/cyber/math/...          math module headers
include/cyber/memory/...        allocator and container headers
include/cybergraphics/          stable C ABI (empty until Phase 25)
lib/cyber_core.lib              static module library
lib/cyber_math.lib              static module library
lib/cyber_memory.lib            static module library
lib/cmake/CyberGraphics/...     package config and exported targets
share/CyberGraphics/...         LICENSE and notices
```

The package consumer test in `tests/integration/package_consumer` and the
source consumer in `tests/integration/source_consumer` run in CI on Windows and
Linux, so neither path can silently break.

## Target names

| Build target | Installed target | Use |
|---|---|---|
| `cyber::graphics_api` | `cyber::graphics_api` | The stable product contract. |
| `cyber::core` | `cyber::core` | Core primitives. |
| `cyber::math` | `cyber::math` | Vectors, matrices, projections, geometry. |
| `cyber::memory` | `cyber::memory` | Allocators, containers, strings, and generation handles. |
| `cyber::rhi`, ... | same | Added when their phases start. |

Build-internal warning policy and test targets are not exported.

## Runtime contract

The stable ABI lives in `include/cybergraphics/` and is documented in
[abi-policy.md](abi-policy.md). It is not implemented until Phase 25. The rules
that matter to Cyber Engine now:

- **C types only.** No STL, exceptions, RTTI, or C++ objects cross the plugin
  boundary.
- **Versioned structs.** Every struct starts with `size` and `version`.
- **Opaque handles.** The host never depends on object layout.
- **One plugin entry point.** It returns a function table; entries are appended,
  never reordered.
- **Capability negotiation.** The host queries features before using them.
- **Error codes.** Recoverable failures return `cg_result`; a message callback
  carries detail.
- **Explicit ownership.** The host owns host memory; the plugin owns plugin
  memory.

### Host responsibilities

- Create and own the window and input loop.
- Provide the frame timestamp and delta time.
- Publish immutable frame data to the renderer.
- Decide when to resize and when to present.
- Load and unload the plugin, and keep the plugin's allocator alive until all
  objects are destroyed.

### Renderer responsibilities

- Own the GPU device, queues, swapchain, and renderer-side resources.
- Validate every handle and command in debug builds.
- Recover from resize, minimize, device-lost, and out-of-memory where the
  contract says recovery is possible.
- Report failures with a code and a readable message.
- Never call back into gameplay code from the render thread unless the callback
  is documented as render-thread work.

### Threads

The intended model is:

```text
Cyber Engine game thread
  -> immutable frame packet
  -> renderer command queue
CyberGraphics render thread
  -> scene update, culling, graph, command recording, submit, present
```

The single-threaded mode is permanent and is the reference for debugging. See
[threading.md](threading.md).

## Versioning

- CyberGraphics uses semantic versioning once a public API exists.
- The plugin reports its ABI major version at load time.
- A host built against minor `N` can load a plugin built against minor `N+1` if
  the function table is additive and the major version matches.
- A major mismatch is rejected with a clear message before any resource is
  created.
- Struct `size` fields allow a newer plugin to accept an older host's smaller
  struct.
- Module C++ headers are source-compatible within a release but are not a
  binary SDK. Only `include/cybergraphics/` is binary-stable.

## Packaging and distribution

A renderer distribution contains:

- the plugin binary (`cyber_graphics.dll`, later `.so`/`.dylib`);
- the stable headers used to build the host;
- `LICENSE` and `THIRD_PARTY_NOTICES.md`;
- a version manifest with the ABI version, build configuration, compiler, and
  commit hash;
- optional symbols and PDBs, kept separately from the runtime package.

Do not redistribute driver SDK files. `d3dcompiler_47.dll` and similar system
components are provided by Windows.

## Integration checklist

Before Cyber Engine depends on a CyberGraphics build:

- [ ] `find_package(CyberGraphics CONFIG)` succeeds from a clean prefix.
- [ ] The package consumer builds on Windows and Linux in CI.
- [ ] The source consumer builds with tests, samples, and install disabled.
- [ ] The plugin loads with the expected ABI major version.
- [ ] A mismatched major version is rejected cleanly.
- [ ] Capability queries answer before optional features are used.
- [ ] Resize, minimize, shutdown, and device-lost are exercised.
- [ ] The renderer runs with the host's chosen `/MD` or `/MT` runtime.
- [ ] The host owns the window; the renderer owns the device.
- [ ] No game system, editor, or audio code is required by the renderer.
- [ ] The license and notices ship with the binary.

## What not to do

- Do not link a plugin built with `/MT` into a host built with `/MD`.
- Do not pass `std::string`, `std::vector`, or a C++ exception across the ABI.
- Do not let the renderer create the OS window; Cyber Engine owns it.
- Do not make Cyber Engine include module `src/` directories.
- Do not install build-internal warning flags or test targets into the SDK.
- Do not freeze the ABI before the renderer design is settled. The package and
  target layout are stable now; the renderer interface arrives in Phase 25.

## Current status

- Source and package consumption: implemented; both consumer projects run in CI.
- Core module: `cyber_core` with version metadata.
- Math module: `cyber_math` with vectors, matrices, projections, geometry,
  color, and half floats.
- Stable C ABI: reserved, not yet implemented.
- GPU plugin: planned, Phase 14 onward.
- Software renderer plugin: planned, Phase 6 onward.
