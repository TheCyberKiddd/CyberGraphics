# cyber_perf — module contract

**Kind:** service
**Layer:** 1
**Order:** 11
**Status:** active
**Owner:** @TheCyberKiddd

## Purpose

Performance instrumentation seam for CPU and GPU markers, counters, frame
statistics, budgets, and export. Phase 5 activates the interface and the null
implementation; later phases add storage, aggregation, and capture.

## Public API

`include/cyber/perf/`

| Header | Contents |
|---|---|
| `profiler.h` | `Profiler`, `NullProfiler`, `NullProfilerInstance`. |
| `perf.h` | Umbrella include. |

Only headers under this path are public. Everything under `src/` is private.
Public headers are self-contained.

### Contract

`Profiler` is a non-owning interface. Implementations must be safe for calls
from multiple worker threads when passed to `cyber_jobs::JobSystem`.

- `BeginScope(const char* name)` starts a named scope.
- `EndScope()` closes the most recent scope on the calling thread.
- `Counter(const char* name, f64 value)` records a named scalar.

The null implementation does nothing. The interface does not allocate in the
marker path. A null profiler pointer is treated as `NullProfilerInstance()`.

## Dependencies

Allowed: cyber_core, cyber_platform

Forbidden: cyber_rhi, cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- Disabled instrumentation has near-zero cost and never allocates.
- `BeginScope`/`EndScope` are balanced by the caller on each thread.
- The interface does not own names or user data; names must outlive the call
  when the implementation stores them.
- Implementations used by workers are thread-safe.
- No RHI or renderer dependency; GPU timestamp collection is supplied by RHI
  code later through this interface.

## Non-goals

- RHI timestamp query implementation.
- Profiler visualization or editor UI.
- Frame capture and binary export in the Phase 5 seam.
- A global mutable profiler singleton. Callers pass a profiler explicitly.

## Tests

`perf_profiler`: null behavior, counting implementation, and counter delivery.

`perf_public_headers`: every public header compiles together.

## Budgets

Disabled marker cost below 1 ns. The enabled interface path performs no
allocation; implementations may use their own preallocated storage. Frame and
GPU budgets are recorded in later performance phases.

## Roadmap

Phases 5, 15, 36.
