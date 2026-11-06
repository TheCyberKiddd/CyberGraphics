# 0025 — Scene proxy ownership and culling

- **Status:** proposed
- **Date:** 2026-11-06
- **Deciders:** @TheCyberKiddd
- **Related:** phase 22, [design note](../design/phase-22-scene-culling.md),
  ADR 0024

## Context

Phase 22 needs renderer-side copies of game-authored render data, ordered
game-to-render updates, parallel visibility tests, and generation-checked
handles. `cyber_scene` is not allowed to depend on `cyber_renderer`, so it
cannot own a second render queue or call `RenderThread` directly.

## Decision

Keep the existing Phase 21 `RenderThread` as the sole producer-to-render-owner
queue. Add an allocator-backed `SceneCommandWriter` that emits bounded,
versioned proxy commands as frame bytes, and apply them to a render-owner-only
`Scene` from the existing frame callback. Store proxies in a flat contiguous
array and parallelize independent per-proxy AABB tests through `cyber_jobs`;
add a spatial hierarchy only if measured data justifies it.

## Consequences

### Positive

- Preserves the established single-producer queue, copied immutable packet,
  render-owner lifetime, and ordered shutdown rules.
- Keeps scene independent from renderer and RHI implementation details.
- Provides deterministic handles, output order, and a brute-force oracle.
- Makes capacities and queue backpressure visible to the caller.

### Negative

- The host must configure the writer's packet limit to fit the render queue's
  frame-data limit and retry the same sealed packet after `Busy`.
- The first implementation scans every allocated slot and may waste work for
  sparse scenes; measurements decide whether a spatial structure is warranted.

### Neutral

- Packet bytes are an in-process command format, not a persistent or
  cross-version asset format.
- LOD selection uses screen-space bound size, not a geometry simplification
  algorithm; mesh generation remains Phase 23.

## Alternatives considered

| Alternative | Why not |
|---|---|
| Put scene commands directly in `cyber_renderer` | Violates the module dependency direction and combines scene data with RHI/render-graph policy. |
| Add a second scene-specific concurrent queue | Duplicates Phase 21 ownership, backpressure, ordering, and shutdown mechanisms. |
| Build a BVH before measuring the flat scan | Adds update and synchronization complexity before evidence that the baseline misses budget. |

## Follow-up

Implement `cyber_scene` and record the 100,000-proxy benchmark. Revisit BVH/grid
work only if the flat scan exceeds the measured budget.
