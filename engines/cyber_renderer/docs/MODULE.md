# cyber_renderer — module contract

**Kind:** graphics
**Layer:** 3
**Order:** 30
**Status:** active
**Owner:** @TheCyberKiddd

## Purpose

Phase 17's renderer-side shader integration, Phase 18's material system, and
Phase 19's frame data streaming and descriptor allocation:
compiler-reflected RHI layouts, vertex input layouts, asynchronous
graphics-pipeline creation, frame-boundary publication, fallback pipelines,
deferred retirement, immutable material definitions, mutable instances,
reflected parameter blocks, texture slots, text serialization, sorted draw
records, fence-safe per-frame upload streams, generation-counted persistent
descriptor indices, per-frame transient indices, and capability-gated bindless
tables. Later phases add render graphs, lighting, and GPU-driven rendering.

## Public API

`include/cyber/renderer/`

`reflected_layout.h` derives RHI layouts from `cyber_shaders::Reflection`,
including the material parameter buffer at `b1, space2`.
`hot_pipeline.h` builds graphics pipelines asynchronously and publishes them
at `BeginFrame`. `material.h` validates immutable material definitions against
shader reflection, creates mutable instances with explicit GPU uploads, sorts
draw keys, and parses/writes bounded `.cybermat` documents. Definitions borrow
their pipeline; instances borrow their definition, device, and allocator, and
never own resolved texture views. Only headers under this path are public.
Everything under `src/` is private. Public headers must compile on their own
and must not include another module's private headers.
Phase 19 adds a frame-slot-bound upload stream with checked aligned allocation,
persistent and transient descriptor allocators, and finite bindless resource
tables with a conventional fallback when `Feature::BindlessResources` is
absent.
Phase 20 adds a setup/execute render graph with generation-checked logical
resources, imported and transient buffers/textures, inferred pass hazards,
export- and side-effect-rooted pass culling, descriptor-compatible transient
aliasing, automatic RHI barriers, and bounded text/Graphviz diagnostics.
Imported resources remain caller-owned; the graph owns only transient physical
resources it creates. Passes execute in deterministic declaration order on a
caller-owned recording command list; submission and cross-queue synchronization
remain the caller's responsibility. The caller acknowledges successful
submission before reusing a graph; a recorded command list that is discarded
instead invalidates that graph instance for reuse. Before destroying the graph,
the caller discards any unsubmitted recording and waits until submitted work no
longer references graph-owned transients.

## Dependencies

Allowed: cyber_core, cyber_memory, cyber_jobs, cyber_rhi, cyber_shaders

Forbidden: cyber_graphics, cyber_ui, tools

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- All GPU work goes through cyber_rhi; no backend headers.
- Reflection and layouts are derived from compiler output, not backend
  reflection.
- Material parameter names, types, and byte offsets match reflected members;
  material buffers are b1/space2, while b0/space0 remains push constants.
- Material uploads are explicitly recorded on a graphics-capable command list;
  the caller orders submission, in-flight updates, and resource destruction.
- Stream slots are writable only after their frame fence completes; descriptor
  indices are generation checked and transient indices reset only on a safe
  frame-slot reuse.
- Bindless access is enabled only when the device reports the feature; table
  mutations are restricted to safe frame slots and unused entries hold caller
  supplied fallback resources.
- Graph setup callbacks declare all resource reads and writes before execution;
  pass callbacks cannot submit commands or access resources outside their
  declared context.
- Transient aliases require identical RHI descriptions and non-overlapping
  live-pass intervals; all graph-generated transitions are recorded before the
  corresponding pass executes.
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

Null-backend reflected-layout, hot-pipeline, material-instance, material-file,
render-graph dependency/culling/aliasing/barrier tests, and public-header tests;
later phases add golden images, per-pass GPU timings, and scene benchmarks.

## Budgets

Per-pass GPU budgets live in docs/perf. Frame-time percentiles are tracked.

## Roadmap

Phases 17, 18, 19, 20, 21, 22, 23, 26, 27, 28, 29, 30, 31, 32, 33, 37.
