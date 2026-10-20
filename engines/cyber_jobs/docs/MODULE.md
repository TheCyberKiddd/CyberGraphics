# cyber_jobs — module contract

**Kind:** service
**Layer:** 1
**Order:** 12
**Status:** planned
**Owner:** @TheCyberKiddd

## Purpose

Job system: thread pool, work-stealing queues, dependency counters, and parallel-for.

## Public API

`include/cyber/jobs/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: cyber_core, cyber_memory, cyber_platform, cyber_perf

Forbidden: cyber_rhi, cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- A job never waits on work that cannot make progress.
- Shutdown is deterministic.
- Oversubscription is tested explicitly.

## Non-goals

- Fibers in the first pass.
- GPU scheduling.

## Tests

10,000 jobs, dependency graphs, oversubscription, shutdown stress, and TSan on Linux.

## Budgets

10,000 jobs scale across all cores; record scaling at 1, 2, 4, and N workers.

## Roadmap

Phases 5.
