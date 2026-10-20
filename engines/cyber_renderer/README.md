# cyber_renderer

**Status:** planned · **Layer:** 3 · **Kind:** graphics

Renderer architecture: render graph, passes, materials, PBR, shadows, IBL, post-processing, GI, and GPU-driven rendering.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/renderer/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The folder exists from day one so the architecture is navigable. A CMake
target is added only when the module's phase starts.

## Roadmap

Phases 18, 19, 20, 21, 22, 23, 26, 27, 28, 29, 30, 31, 32, 33, 37.
