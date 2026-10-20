# cyber_memory — module contract

**Kind:** foundation
**Layer:** 0
**Order:** 2
**Status:** planned
**Owner:** @TheCyberKiddd

## Purpose

Allocator interface, linear, stack, pool, free-list and ring allocators, tracking, containers, strings, spans, and generation-counted handles.

## Public API

`include/cyber/memory/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: cyber_core

Forbidden: cyber_platform, cyber_rhi, cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- Every runtime allocation goes through the allocator interface.
- Handles carry an index and a generation.
- Every container documents its copy and move rules.

## Non-goals

- OS virtual memory reservation (cyber_platform).
- GPU memory allocation (cyber_rhi).

## Tests

Container stress tests, deliberate-leak report, stale-handle detection, ASan and UBSan runs.

## Budgets

Allocator overhead documented per allocator. No hidden global allocations.

## Roadmap

Phases 4.
