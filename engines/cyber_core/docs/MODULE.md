# cyber_core — module contract

**Kind:** foundation
**Layer:** 0
**Order:** 0
**Status:** scaffolded
**Owner:** @TheCyberKiddd

## Purpose

Fixed-width types, assertions, logging, Result, configuration switches, versioning, hashing, and other dependency-free primitives.

## Public API

`include/cyber/core/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: None

Forbidden: cyber_platform, cyber_rhi, cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- No dependency on any other CyberGraphics module.
- Assertions and logging must not allocate on the failure path.
- Public headers are self-contained and compile on their own.

## Non-goals

- Containers and allocators (cyber_memory).
- Math types (cyber_math).
- Platform services (cyber_platform).
- Rendering (cyber_rhi, cyber_renderer).

## Tests

Unit tests for Result, assertion formatting, log channels and sinks, and version macros.

## Budgets

Assert path: no allocation. Log path: thread-safe and bounded.

## Roadmap

Phases 2.
