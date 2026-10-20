# cyber_perf

**Status:** planned · **Layer:** 1 · **Kind:** service

Performance instrumentation: CPU and GPU markers, counters, frame statistics, budgets, and export.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/perf/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The folder exists from day one so the architecture is navigable. A CMake
target is added only when the module's phase starts.

## Roadmap

Phases 5, 15, 36.
