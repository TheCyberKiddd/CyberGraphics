# cyber_renderer

**Status:** active · **Layer:** 3 · **Kind:** graphics

Phase 17 adds compiler-reflected binding layouts and asynchronous graphics
pipelines with fallback selection, frame-boundary swaps, and deferred retirement.
Later phases add render graphs, materials, lighting, and GPU-driven rendering.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/renderer/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The module is active; its dependencies are limited to the RHI, shader compiler,
job system, memory, and core.

## Roadmap

Phases 17, 18, 19, 20, 21, 22, 23, 26, 27, 28, 29, 30, 31, 32, 33, 37.
