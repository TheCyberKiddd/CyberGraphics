# Phase 22 — Scene, render proxies, and culling

Phase 22 introduces the active `cyber_scene` module. Game-thread proxy changes
are copied into bounded command packets and submitted through Phase 21's
`RenderThread`; the render owner applies them to a contiguous proxy store.
Generation-checked handles, mesh-instance/point-light/camera records,
transform-derived world AABBs, dirty flags, parallel per-view frustum culling,
and screen-space mesh LOD selection are implemented.

**Status:** implementation and local validation complete. The master roadmap
checkbox remains pending commit, as required by the repository's evidence rule.
See the [Phase 22 report](../reports/phase-22.md), [design note](../design/phase-22-scene-culling.md),
[reading notes](../notes/phase-22.md), and [ADR 0025](../adr/0025-scene-proxy-ownership-and-culling.md).

## Public API

Include
[`scene.h`](../../engines/cyber_scene/include/cyber/scene/scene.h). The game
thread owns a `SceneCommandWriter`, and the render callback owns a `Scene`.
Configure compatible proxy and command capacities; configure
`RenderThreadDesc::maxFrameDataBytes` to at least `writer.MaxPacketBytes()`.

```cpp
auto writerResult =
    cyber::scene::SceneCommandWriter::Create({100000, 256}, gameAllocator);
auto sceneResult = cyber::scene::Scene::Create({100000, 256}, renderAllocator);
if (!writerResult || !sceneResult)
{
    return;
}

auto writer = std::move(writerResult.Value());
auto scene = std::move(sceneResult.Value());

cyber::scene::MeshInstanceDesc mesh{};
mesh.meshId = meshId;
mesh.materialId = materialId;
mesh.transform = worldTransform;
mesh.localBounds = meshBounds;
auto handle = writer.CreateMeshInstance(mesh);
if (!handle)
{
    return;
}

const auto packet = writer.Seal();
const auto submitted = renderThread.SubmitFrame(packet);
if (submitted)
{
    const auto committed = writer.Commit();
    if (!committed)
    {
        return;
    }
}
else if (submitted.Error().code == cyber::core::ErrorCode::Busy)
{
    // Retry this same sealed packet; its bytes remain immutable.
}
```

In the `RenderThread` frame callback, apply `frame.data` with
`scene.ApplyCommands(frame.data)`. Call `scene.Cull(view, jobs, resultAllocator)`
on the scene owner to obtain deterministic, slot-ordered visibility entries.
Call `Scene::AcknowledgeDirty` after the renderer consumes the corresponding
proxy data.

## Ownership and behavior

- The writer and scene preallocate bounded command, handle, proxy, validation,
  and culling storage. Queue `Busy` leaves a sealed packet unchanged for retry;
  call `Commit` only after queue acceptance.
- The scene validates an entire packet before applying any command. Commands
  retain packet order, including destroy/recreate generation changes.
- World AABBs conservatively transform local bounds. Plane-touching and
  straddling boxes remain visible. Parallel results compact in slot order.
- LOD thresholds are strictly descending projected-radius values in pixels.
  Camera proxies are stored but are not visibility-list entries; point lights
  use range-derived bounds and are culled.
- The 100,000-proxy flat scan is the baseline. This phase adds no BVH/grid or
  geometry processing.

## Validation and measured budget

On the 4-core/4-thread Intel Core i5-6400, the Release benchmark's 21 measured
samples produced a 1.777 ms median and 2.881 ms p95 for 100,000 proxies against
a 5 ms p95 budget. All 1,681 visible handles and their order matched the
independent serial brute-force reference. Full validation commands and results
are recorded in the [report](../reports/phase-22.md).

## Known gaps

- The benchmark proves CPU culling, not native GPU rendering or cross-vendor
  frame performance.
- Mesh/material IDs are copied identifiers; asset loading and cooked geometry
  are later-phase work.
- The flat scan may be suboptimal for sparse visibility. Revisit a spatial
  structure only if measurements exceed the recorded budget.
- Command packets are versioned in-process bytes, not a persistent or
  cross-build serialization format.
