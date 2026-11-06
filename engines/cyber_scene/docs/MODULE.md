# cyber_scene — module contract

**Kind:** graphics
**Layer:** 2
**Order:** 23
**Status:** active
**Owner:** @TheCyberKiddd

## Purpose

Renderer-owned scene data: copied mesh-instance, light, and camera proxies;
generation-checked proxy handles; world bounds; parallel frustum culling; and
screen-space LOD selection. Phase 22 transports bounded proxy commands through
the existing render-thread frame queue. Phase 23 adds geometry processing.

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

- Proxies contain copied renderer data; no game-thread pointer is retained.
- Scene mutation belongs to the render owner; worker jobs read immutable scene
  storage during culling.
- Proxy handles are generation checked and stale handles never resolve.
- Culling is conservative, deterministic, and matches an independent
  brute-force reference.
- Transform, bounds, and dirty-state data stay in bounded contiguous storage.
- Scene command bytes are versioned, bounded, and applied in queue order.

## Non-goals

- Gameplay scene graph.
- Physics and animation.

## Tests

Command validation, proxy lifecycle and stale handles, brute-force culling
comparison, plane-straddling and plane-touching cases, LOD selection,
public-header compilation, and a 100,000-object benchmark.

## Budgets

100,000-proxy cull within a 5 ms p95 budget on the documented 4-core reference
machine; see [`docs/reports/phase-22.md`](../../../docs/reports/phase-22.md).

## Roadmap

Phase 22 implementation and validation are tracked in
[`docs/graphics/PHASE_22.md`](../../../docs/graphics/PHASE_22.md); Phase 23
continues geometry processing.
