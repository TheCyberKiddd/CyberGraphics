# Phase 22 design: scene proxies and culling

## Goal and scope

Add a renderer-owned `cyber_scene` module for copied mesh-instance, point-light,
and camera proxies; deterministic generational handles; bounded update packets
that can travel as immutable Phase 21 `RenderThread` frame data; parallel
frustum culling; and screen-space LOD selection. The acceptance workload is
100,000 proxies compared exactly with a serial brute-force reference and timed
against a recorded budget.

The module does not add a gameplay scene graph, asset loading, animation,
occlusion, a BVH/grid, a renderer dependency, or a new queue. The caller owns
the game-side data and `RenderThread`; `SceneCommandWriter` copies only values
needed by the renderer into a packet, and the render callback applies that
packet to its exclusively owned `Scene`.

## Interfaces and ownership

- `SceneCommandWriter` is the game-thread-side producer. It uses preallocated
  command and handle storage, exposes Create/Update/Destroy for mesh, light,
  and camera proxy values, seals a byte view for `RenderThread::SubmitFrame`,
  and is committed only after the queue accepts the packet. A full command
  buffer fails explicitly; a `Busy` queue response leaves the sealed bytes
  unchanged for retry.
- `Scene::ApplyCommands` is called by the render-owner callback. It validates
  packet size, version, operation, handle generation, and proxy values before
  mutating its private state. Each accepted operation is applied in packet
  order; queue order therefore preserves destroy/recreate generations.
- A `Scene` is single-owner mutable state. `Cull` is called by that owner and
  may use `cyber_jobs` workers only to read stable proxy storage and write
  disjoint per-slot results. No job accesses RHI objects or caller-owned
  gameplay state.
- Proxy records remain contiguous and contain transform, local/world bounds,
  classification, LOD data, and dirty flags. World AABBs are recomputed when
  transform or local bounds change.
- AABB plane tests are inclusive: a plane-touching or straddling object is
  retained. Visibility compaction follows slot order for deterministic output.
- Mesh LOD selection uses projected bound radius in pixels:
  `radius * projectionScalePixels / max(distance - radius, nearDistance)`.
  Descending pixel thresholds select progressively coarser LODs.
- Camera proxies are retained as scene data but are not renderable visibility
  entries. Point lights use a range-derived world AABB and participate in culling.

## Allocation and failure behavior

Both writer and scene require an explicit allocator and maximum proxy/command
capacities. They reserve their bounded storage during creation; culling reuses
scene-owned scratch and allocates only the returned visibility list from the
caller's allocator. Invalid descriptors, stale handles, malformed packets,
capacity exhaustion, and allocation failures return project `Error` values.
No global state, exceptions, hidden thread, or per-object heap allocation is
introduced.

## Verification

Tests cover every public operation, malformed packets, proxy generations,
writer/queue retry behavior, transform-to-world-bound updates, inclusive plane
cases, camera/light/mesh behavior, LOD thresholds, deterministic ordering, and
parallel results against an independent serial reference. A Release benchmark
uses 100,000 deterministic proxies and records CPU, worker count, visible count,
and elapsed time. The budget is set from measured local evidence rather than
claimed in advance.

## Reading notes

The [Phase 22 reading notes](../notes/phase-22.md) record the sources and
observations. The design is informed by Lighthouse3D,
[Testing Boxes](https://www.lighthouse3d.com/tutorials/view-frustum-culling/geometric-approach-testing-boxes/)
(reject only when all box support is outside the same plane), and LearnOpenGL,
[Frustum Culling](https://learnopengl.com/Guest-Articles/2021/Scene/Frustum-Culling)
(use conservative simple bounds against the six frustum planes). Microsoft
documents the matching [BoundingFrustum/AABB intersection API](https://learn.microsoft.com/en-us/windows/win32/api/directxcollision/nf-directxcollision-boundingfrustum-intersects%28constboundingbox_%29).
The implementation uses CyberGraphics math and copies no external code.
