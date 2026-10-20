# cyber_stability

**Status:** planned · **Layer:** 1 · **Kind:** service

Validation, diagnostics, crash handling, leak tracking, device-lost handling, and the fuzz harness.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/stability/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The folder exists from day one so the architecture is navigable. A CMake
target is added only when the module's phase starts.

## Roadmap

Phases 2, 15, 40.
