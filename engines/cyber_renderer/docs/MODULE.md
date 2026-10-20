# cyber_renderer — module contract

**Kind:** graphics
**Layer:** 3
**Order:** 30
**Status:** planned
**Owner:** @TheCyberKiddd

## Purpose

Renderer architecture: render graph, passes, materials, PBR, shadows, IBL, post-processing, GI, and GPU-driven rendering.

## Public API

`include/cyber/renderer/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: cyber_core, cyber_math, cyber_memory, cyber_jobs, cyber_perf, cyber_stability, cyber_rhi, cyber_shaders, cyber_assets, cyber_scene

Forbidden: cyber_graphics, cyber_ui, tools

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- All GPU work goes through cyber_rhi; no backend headers.
- Passes declare reads and writes; barriers are graph-generated.
- No game logic, only render proxies.

## Non-goals

- Product plugin ABI (cyber_graphics).
- Editor integration.

## Tests

Graph culling and aliasing tests, golden images, per-pass GPU timings, and scene benchmarks.

## Budgets

Per-pass GPU budgets live in docs/perf. Frame-time percentiles are tracked.

## Roadmap

Phases 18, 19, 20, 21, 22, 23, 26, 27, 28, 29, 30, 31, 32, 33, 37.
