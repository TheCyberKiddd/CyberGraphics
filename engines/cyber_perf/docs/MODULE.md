# cyber_perf — module contract

**Kind:** service
**Layer:** 1
**Order:** 11
**Status:** planned
**Owner:** @TheCyberKiddd

## Purpose

Performance instrumentation: CPU and GPU markers, counters, frame statistics, budgets, and export.

## Public API

`include/cyber/perf/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: cyber_core, cyber_platform

Forbidden: cyber_rhi, cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- Instrumentation is opt-in and near-zero cost when disabled.
- No allocation on the marker hot path.
- Thread-safe from game, render, and worker threads.

## Non-goals

- RHI timestamp query implementation (cyber_rhi consumes and produces data).
- Profiler visualization (cyber_ui).

## Tests

Marker nesting, counter aggregation, export format, and disabled-path cost.

## Budgets

Disabled marker cost below 1 ns; enabled path allocation-free.

## Roadmap

Phases 5, 15, 36.
