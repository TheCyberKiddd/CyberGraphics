# cyber_platform

**Status:** active · **Layer:** 1 · **Kind:** platform

Renderer-agnostic OS services: window, input, time, files, memory mapping,
thread wrappers, and dynamic-library loading.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/platform/` — public headers, self-contained.
- `src/` — private implementation; Win32 under `src/win32/`, null window under
  `src/null/`.
- `tests/` — unit tests owned by this module.

The folder existed before its phase started so the architecture stayed
navigable. The `cyber_platform` target became active in Phase 5.

## Boundary

This module never includes `cyber_rhi` or a renderer. The window is the OS
surface; the RHI and renderer decide what to present on it. Cyber Engine can
consume this target through the installed CMake package or as a submodule.

## Roadmap

Phase 5.
