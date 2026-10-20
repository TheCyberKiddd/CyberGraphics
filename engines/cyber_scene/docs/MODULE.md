# cyber_scene — module contract

**Kind:** graphics
**Layer:** 2
**Order:** 23
**Status:** planned
**Owner:** @TheCyberKiddd

## Purpose

Renderer-side scene: proxies, bounds, frustum culling, spatial index, and LOD selection.

## Public API

`include/cyber/scene/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: cyber_core, cyber_math, cyber_memory, cyber_jobs, cyber_assets

Forbidden: cyber_renderer, cyber_graphics, cyber_ui

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- Proxies are the renderer's copies; game-thread data never leaks into render-thread reads.
- Culling is deterministic and matches the brute-force reference.
- Bounds are tightly packed.

## Non-goals

- Gameplay scene graph.
- Physics and animation.

## Tests

Brute-force culling comparison, plane-straddling cases, and a 100,000-object benchmark.

## Budgets

100,000-object cull within the budget recorded in the phase report.

## Roadmap

Phases 22, 23.
