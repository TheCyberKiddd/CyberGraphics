# cyber_software_renderer — module contract

**Kind:** product
**Layer:** 4
**Order:** 41
**Status:** planned
**Owner:** @TheCyberKiddd

## Purpose

CPU reference renderer: framebuffer, rasterization, depth, textures, shaders, golden images, and the second plugin.

## Public API

`include/cyber/software/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: cyber_core, cyber_math, cyber_memory, cyber_jobs, cyber_assets, cyber_scene, cybergraphics_api

Forbidden: cyber_rhi, cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- No RHI dependency; pure CPU.
- Deterministic output for golden images.
- Implements the same stable API as the GPU renderer.

## Non-goals

- Performance parity with the GPU.
- Production rendering.

## Tests

Golden images at 640x360, raster coverage, clipping, and API conformance.

## Budgets

Golden-image suite runtime budget. Small test resolution.

## Roadmap

Phases 6, 7, 8, 9, 25.
