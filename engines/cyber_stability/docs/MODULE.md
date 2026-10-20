# cyber_stability — module contract

**Kind:** service
**Layer:** 1
**Order:** 13
**Status:** planned
**Owner:** @TheCyberKiddd

## Purpose

Validation, diagnostics, crash handling, leak tracking, device-lost handling, and the fuzz harness.

## Public API

`include/cyber/stability/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: cyber_core, cyber_memory, cyber_platform, cyber_perf

Forbidden: cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- Validation is debug-only unless explicitly enabled.
- Crash paths avoid allocation.
- Every reported error has a stable code and a human-readable message.

## Non-goals

- RHI-specific barrier logic (cyber_rhi provides hooks).
- Editor diagnostics UI.

## Tests

Deliberate leak, invalid handle, crash-handler smoke test, and validation-hook tests.

## Budgets

Disabled validation below 1% CPU. Crash handler writes within a bounded time.

## Roadmap

Phases 2, 15, 40.
