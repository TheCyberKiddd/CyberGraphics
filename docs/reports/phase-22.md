# Phase 22 report: scene, render proxies, and culling

Date: 2026-11-06 · Phase 22 implementation and local validation

## What was built

- Activated `cyber_scene` with a self-contained public API, CMake target,
  package installation, module contract, and source/package consumer coverage.
- Added bounded game-thread `SceneCommandWriter` operations for creating,
  updating, and destroying copied mesh-instance, point-light, and camera
  proxies. Sealed packets use the existing Phase 21 copied `RenderThread`
  queue; scene-side validation is completed before applying any operation.
- Added generation-checked handles, contiguous proxy records with transform,
  local/world AABBs and dirty flags, transform-derived world bounds, and
  explicit dirty-state acknowledgement.
- Added deterministic parallel AABB/frustum culling through `cyber_jobs`,
  per-view visibility lists, and screen-space projected-radius LOD selection.
- Kept a flat scan as the measured baseline; no BVH/grid or geometry pipeline
  was added.
- Added lifecycle, malformed/oversized packet, transactional validation,
  transform-bound, frustum-boundary, LOD, public-header, render-thread queue,
  and 100,000-proxy brute-force benchmark coverage.

## Decisions

See [ADR 0025](../adr/0025-scene-proxy-ownership-and-culling.md), the
[design note](../design/phase-22-scene-culling.md), and
[reading notes](../notes/phase-22.md). The scene module uses Phase 21's frame
queue rather than introducing a second queue and does not depend on
`cyber_renderer` or RHI types.

## Evidence for done-when

The Release benchmark ran on an Intel Core i5-6400 with 4 cores and 4 logical
processors. With four job workers, 100,000 deterministic mesh proxies produced
1,681 visible results. Every visible handle and slot order matched an
independent serial `math::Intersects` reference. Across two warmups and 21
measured culls, the median was 1,777 µs and p95 was 2,881 µs, below the selected
5,000 µs p95 budget.

| Exact command | Result |
|---|---|
| `.\scripts\build.ps1 -Preset debug -Test` | Passed; 104/104 CTest targets. |
| `.\scripts\build.ps1 -Preset release -Test` | Passed; 104/104 CTest targets. |
| `.\scripts\build.ps1 -Preset debug-asan -Test` | Passed; 104/104 CTest targets. |
| `.\build\release\engines\cyber_scene\cyber_scene_bench.exe` | Passed; 100,000 proxies, 1,681 visible and reference handles, 4 workers, median 1,777 µs, p95 2,881 µs. |
| <code>wsl.exe --exec bash -lc "cd /mnt/c/Users/Main/Desktop/CyberGraphics && cmake --build --preset ci-linux-tsan --target cyber_test_scene_proxy cyber_test_scene_render_thread && ctest --test-dir build/ci-linux-tsan -R 'scene_proxy&#124;scene_render_thread' --output-on-failure"</code> | Passed; 2/2 scene tests, no ThreadSanitizer reports. |
| `.\scripts\verify_package.ps1 -Preset release` | Passed; installed `cyber/scene/scene.h`, consumer configured, built, and ran. |
| `. .\scripts\bootstrap.ps1 -Quiet; cmake -S tests\integration\source_consumer -B build\phase22-source-consumer -G Ninja -DCMAKE_BUILD_TYPE=Debug; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; cmake --build build\phase22-source-consumer; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; .\build\phase22-source-consumer\source_consumer.exe; exit $LASTEXITCODE` | Passed; source-tree consumer built and ran. |
| `py -3 scripts\check_structure.py` | Passed; 16 modules and one contract. |
| `py -3 scripts\layer_check.py` | Passed; 16 modules. |
| `.\scripts\format.ps1 -Check` | Passed. |
| `git diff --check` | Passed; no whitespace errors. Git emitted line-ending normalization warnings for JSON/CMake files. |

The benchmark budget is 5 ms p95 for this fixed 100,000-proxy workload. It is
a local CPU-culling target, not a GPU frame-time claim.

## Known gaps

- No native GPU frame or multi-vendor performance claim is made.
- Asset IDs are opaque copied values; geometry and asset loading remain outside
  this phase.
- The flat O(n) scan is measured for this workload but may underperform for
  sparse scenes; spatial indexing remains measurement-gated.
- The master roadmap checkbox is intentionally still unchecked until this
  evidence is committed.
