# cyber_shaders

**Status:** active · **Layer:** 2 · **Kind:** graphics

Phase 16 adds the typed Cyber shader language, compiler, diagnostics, and HLSL
generation. Reflection, permutations, and hot reload follow in Phase 17.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/shaders/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The module is active. Its CMake target and public API are added as Phase 16 is
implemented.

## Roadmap

Phases 16, 17.
