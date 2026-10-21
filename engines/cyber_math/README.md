# cyber_math

**Status:** active · **Layer:** 0 · **Kind:** foundation

Scalar and SIMD math: vectors, matrices, quaternions, transforms, geometry, color, half floats.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/math/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.
- `benchmarks/` — SIMD-versus-scalar throughput checks, not CTest gates.

The folder existed before its phase started so the architecture stayed
navigable. The `cyber_math` target became active in Phase 3.

## Roadmap

Phase 3.
