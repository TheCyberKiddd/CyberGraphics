# cyber_core

**Status:** scaffolded · **Layer:** 0 · **Kind:** foundation

Fixed-width types, assertions, logging, Result, configuration switches, versioning, hashing, and other dependency-free primitives.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/core/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The folder exists from day one so the architecture is navigable. A CMake
target is added only when the module's phase starts.

## Roadmap

Phases 2.
