# cyber_capture

**Status:** planned · **Layer:** 3 · **Kind:** service

Frame capture: record RHI commands and resources, versioned capture format, and replay hooks.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/capture/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The folder exists from day one so the architecture is navigable. A CMake
target is added only when the module's phase starts.

## Roadmap

Phases 39.
