# cyber_ui — module contract

**Kind:** graphics
**Layer:** 3
**Order:** 31
**Status:** planned
**Owner:** @TheCyberKiddd

## Purpose

Immediate-mode UI, debug overlay, text, graphs, console variables, and the render graph viewer.

## Public API

`include/cyber/ui/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: cyber_core, cyber_math, cyber_memory, cyber_perf, cybergraphics_api

Forbidden: cyber_rhi, cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- UI draws only through the graphics API and works on GPU and software renderers.
- No per-frame allocation in steady state.
- Input routing is deterministic.

## Non-goals

- Full editor framework.
- Game UI framework.

## Tests

Golden UI image on both renderers, input routing tests, and a no-allocation steady-state test.

## Budgets

UI CPU cost below the budget recorded in the phase report.

## Roadmap

Phases 38.
