# cyber_math — module contract

**Kind:** foundation
**Layer:** 0
**Order:** 1
**Status:** active
**Owner:** @TheCyberKiddd

## Purpose

Scalar and SIMD math: vectors, matrices, quaternions, transforms, geometry, color, half floats.

## Public API

`include/cyber/math/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

The module exposes:

- `constants.h`, `types.h`, `simd.h` — scalar helpers, namespace aliases, and
  the compile-time SIMD backend query;
- `vec2.h`, `vec3.h`, `vec4.h` — scalar and SSE2 vector arithmetic;
- `mat3.h`, `mat4.h` — column-major matrices and inverse/view/projection
  constructors;
- `quat.h`, `transform.h` — unit quaternion rotation and TRS authoring;
- `aabb.h`, `plane.h`, `ray.h`, `sphere.h`, `frustum.h` — CPU geometry;
- `color.h`, `half.h` — linear/sRGB color and binary16 storage;
- `math.h` — umbrella include.

Matrix access is `m[column][row]`. Matrix composition is right-to-left:
`(A * B) * v == A * (B * v)`.

`Transform` is an authoring record (`p' = T + R * (S * p)`), not a closed
algebra under non-uniform scale. Compose `ToMatrix()` values for hierarchies and
use `Transform::TryInverse(Mat4&)` for the exact inverse.

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

Unit tests for every operation with tolerance helpers, plus a SIMD-versus-scalar benchmark:

- `math_vec`, `math_mat`, `math_projection`, `math_quat`, `math_transform`;
- `math_geometry`, `math_color`, `math_half`, `math_public_headers`;
- `cyber_math_bench` for `Vec4` add throughput on the development machine.

## Budgets

No heap allocation. Record SIMD-versus-scalar results on the target machine.

## Roadmap

Phase 3.
