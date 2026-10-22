# cyber_jobs

**Status:** active · **Layer:** 1 · **Kind:** service

Job system: fixed worker pool, work-stealing queues, dependency counters,
generation-checked handles, parallel-for, and deterministic shutdown.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/jobs/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The folder existed before its phase started so the architecture stayed
navigable. The `cyber_jobs` target became active in Phase 5.

## Scheduling model

Each worker owns a bounded ring deque. Workers pop their own newest work and
steal oldest work from other workers. Dependencies are counters; completion
queues ready dependents. Job records and handles are preallocated, so scheduling
does not touch the heap after startup.

## Roadmap

Phase 5.
