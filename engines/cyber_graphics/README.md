# cyber_graphics

**Status:** planned · **Layer:** 4 · **Kind:** product

The CyberGraphics GPU renderer product: plugin entry, backend selection, frame loop, and implementation of the stable API.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/graphics/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The folder exists from day one so the architecture is navigable. A CMake
target is added only when the module's phase starts.

## Roadmap

Phases 14, 15, 25.
