# cyber_scene

**Status:** planned · **Layer:** 2 · **Kind:** graphics

Renderer-side scene: proxies, bounds, frustum culling, spatial index, and LOD selection.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/scene/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The folder exists from day one so the architecture is navigable. A CMake
target is added only when the module's phase starts.

## Roadmap

Phases 22, 23.
