# cyber_shaders

**Status:** active · **Layer:** 2 · **Kind:** graphics

Phases 16-17 provide the typed Cyber shader language, compiler-owned reflection,
stable keyword permutations, on-disk caching, job-system compilation, file
watching, and renderer-side hot graphics pipelines.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/shaders/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The module is active and has an RHI-independent C++ API.

## Roadmap

Phases 16, 17.
