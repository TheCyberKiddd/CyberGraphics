# cyber_scene

**Status:** active · **Layer:** 2 · **Kind:** graphics

Renderer-owned scene proxies, bounded command packets, parallel frustum culling,
and screen-space LOD selection. See the [Phase 22 summary](../../docs/graphics/PHASE_22.md).

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/scene/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The Phase 22 public API, implementation, tests, and CMake target are in place;
the master roadmap checkbox awaits committed validation evidence.

## Roadmap

Phases 22, 23.
