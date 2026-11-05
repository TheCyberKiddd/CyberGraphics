# cyber_renderer — module contract

**Kind:** graphics
**Layer:** 3
**Order:** 30
**Status:** active
**Owner:** @TheCyberKiddd

## Purpose

Phase 17's renderer-side shader integration: compiler-reflected RHI layouts,
vertex input layouts, asynchronous graphics-pipeline creation, frame-boundary
publication, fallback pipelines, and deferred retirement. Later phases add the
render graph, materials, lighting, and GPU-driven rendering.

## Public API

`include/cyber/renderer/`

`reflected_layout.h` derives RHI layouts from `cyber_shaders::Reflection`.
`hot_pipeline.h` builds graphics pipelines asynchronously and publishes them at
`BeginFrame`. Only headers under this path are public. Everything under `src/`
is private. Public headers must compile on their own and must not include
another module's private headers.

## Dependencies

Allowed: cyber_core, cyber_memory, cyber_jobs, cyber_rhi, cyber_shaders

Forbidden: cyber_graphics, cyber_ui, tools

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- All GPU work goes through cyber_rhi; no backend headers.
- Reflection and layouts are derived from compiler output, not backend
  reflection.
- Pipeline changes publish only at frame boundaries; replaced GPU objects are
  retained through the configured frames-in-flight interval.
- The owning thread calls public renderer methods; background jobs own their
  build data and use the thread-safe RHI device.
- Passes declare reads and writes; barriers are graph-generated.
- No game logic, only render proxies.

## Non-goals

- Product plugin ABI (cyber_graphics).
- Editor integration.

## Tests

Null-backend reflected-layout and hot-pipeline tests; later phases add graph
culling and aliasing tests, golden images, per-pass GPU timings, and scene
benchmarks.

## Budgets

Per-pass GPU budgets live in docs/perf. Frame-time percentiles are tracked.

## Roadmap

Phases 17, 18, 19, 20, 21, 22, 23, 26, 27, 28, 29, 30, 31, 32, 33, 37.
