# cyber_graphics — module contract

**Kind:** product
**Layer:** 4
**Order:** 40
**Status:** planned
**Owner:** @TheCyberKiddd

## Purpose

The CyberGraphics GPU renderer product: plugin entry, backend selection, frame loop, and implementation of the stable API.

## Public API

`include/cyber/graphics/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: cyber_core, cyber_math, cyber_memory, cyber_platform, cyber_jobs, cyber_perf, cyber_stability, cyber_rhi, cyber_shaders, cyber_assets, cyber_scene, cyber_renderer, cybergraphics_api

Forbidden: cyber_software_renderer, tools

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- Implements only the stable API in include/cybergraphics/.
- No public C++ types cross the plugin boundary.
- Capability negotiation is explicit.

## Non-goals

- CPU rendering (cyber_software_renderer).
- Gameplay.

## Tests

Plugin ABI compatibility, backend switch, and golden images against the software reference.

## Budgets

Frame budgets live in docs/perf. CPU submission cost measured.

## Roadmap

Phases 14, 15, 25.
