# cyber_math — module contract

**Kind:** foundation
**Layer:** 0
**Order:** 1
**Status:** planned
**Owner:** @TheCyberKiddd

## Purpose

Scalar and SIMD math: vectors, matrices, quaternions, transforms, geometry, color, half floats.

## Public API

`include/cyber/math/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: cyber_core

Forbidden: cyber_platform, cyber_rhi, cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- Right-handed, +Y up, column vectors, column-major matrices, 0..1 reverse-Z.
- Scalar and SIMD paths produce identical results within documented tolerance.
- No heap allocation in math operations.

## Non-goals

- SIMD dispatch beyond SSE/AVX2 in the first pass.
- Scene graph and animation.
- GPU math types.

## Tests

Unit tests for every operation with tolerance helpers, plus a SIMD-versus-scalar benchmark.

## Budgets

No heap allocation. Record SIMD-versus-scalar results on the target machine.

## Roadmap

Phases 3.
