# cyber_capture — module contract

**Kind:** service
**Layer:** 3
**Order:** 32
**Status:** planned
**Owner:** @TheCyberKiddd

## Purpose

Frame capture: record RHI commands and resources, versioned capture format, and replay hooks.

## Public API

`include/cyber/capture/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: cyber_core, cyber_memory, cyber_platform, cyber_rhi

Forbidden: cyber_graphics, cyber_renderer

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- Capture format is versioned and backend-neutral.
- Replay is deterministic within tolerance.
- Capture overhead is bounded and opt-in.

## Non-goals

- GUI inspector (tools/capture).
- PIX or RenderDoc replacement.

## Tests

Record and replay round-trip, cross-backend replay, and format version compatibility.

## Budgets

Capture overhead recorded. No overhead when disabled.

## Roadmap

Phases 39.
