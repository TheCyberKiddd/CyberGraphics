# cyber_perf

**Status:** active · **Layer:** 1 · **Kind:** service

Performance instrumentation seam: CPU and GPU markers, counters, frame
statistics, budgets, and export.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/perf/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

## Current scope

Phase 5 activates the non-allocating `Profiler` interface and a null
implementation. The job system emits balanced scope markers. Storage,
aggregation, GPU timestamps, capture, and export arrive in the performance
phases.

## Roadmap

Phases 5, 15, 36.
