# 0005 — Math representation, SIMD, and fallibility

- **Status:** accepted
- **Date:** 2026-10-20
- **Deciders:** CyberEngine (owner)
- **Related:** [math conventions](../math-conventions.md), ADR 0002, Phase 3

## Context

Phase 3 introduces the numeric foundation that every renderer module will use
for years: vectors, matrices, quaternions, transforms, projections, geometry,
color, and half floats. The representation and API shape are expensive to
change once cameras, importers, shaders, and golden images depend on them.

The project needs predictable layouts for GPU uploads, no heap allocation,
C++20 with exceptions and RTTI disabled, and a scalar/SIMD agreement contract.
It also needs a clear answer for singular matrices without inventing a global
error state.

## Decision

1. **Packed scalar `Vec3`, aligned `Vec4` and `Mat4`.** `Vec3` is 12 bytes and
   4-byte aligned for interleaved vertex data. `Vec4` is 16 bytes and 16-byte
   aligned. `Mat4` stores four `Vec4` columns, is 64 bytes, and is 16-byte
   aligned. All matrices are column-major with `m[column][row]`.
2. **SSE2 first, scalar fallback, no runtime dispatch.** `simd.h` selects SSE2
   at compile time when the target provides it. `Vec4` operations use SSE2;
   other types are scalar. AVX2, NEON, and runtime dispatch are later changes
   behind the same API.
3. **Inversion is fallible through an out-parameter.** `TryInverse(out)` returns
   `false` for a singular matrix and leaves the output untouched. The
   implementation uses `f64` Gaussian elimination with partial pivoting and a
   relative threshold of `1e-12 * maxAbs`. No exception, allocation, `Result`,
   or global error state is involved.
4. **Projection names encode depth.** `PerspectiveReverseZ`,
   `PerspectiveInfiniteReverseZ`, and `OrthographicReverseZ` use 0..1 clip depth
   and map near to `1`, far to `0`, per ADR 0002. There is no ambiguous
   `Perspective` alias.
5. **Quaternions are unit rotation quaternions.** Layout is `(x, y, z, w)`,
   identity is `(0, 0, 0, 1)`, and multiplication is Hamilton. Euler input is
   YXZ with `q = qY * qX * qZ`; Euler extraction is not provided. `Transform`
   is an authoring record (`p' = T + R * (S * p)`), not a closed algebra under
   non-uniform scale. It has no `Transform * Transform` operator;
   `TryInverse(Mat4&)` returns the exact matrix inverse, and hierarchy
   composition uses `ToMatrix()`.
6. **Empty AABBs are explicit.** An `Aabb` carries an `empty` flag and never
   uses inverted infinities to mean "empty".
7. **Half floats are storage.** `Half` is a 2-byte IEEE binary16 value with
   round-to-nearest-even conversion and canonicalized NaN payloads. Arithmetic
   happens in `f32`.
8. **Tolerance is part of the test contract.** The project runner gains
   absolute-tolerance assertions; math tests never rely on exact float equality
   unless the operation is exact by definition.

## Consequences

### Positive

- GPU upload and vertex layouts are predictable without conversion helpers.
- The hot `Vec4` path is vectorized on the reference toolchains without a
  runtime feature check.
- Callers cannot silently use an invalid inverse.
- Projection and depth mistakes are harder to introduce because the names and
  tests state the convention.
- Geometry, color, and half-float behavior are explicit before later modules
  depend on them.

### Negative

- Public headers expose SSE2 intrinsics on supported targets. This is a
  compiler-header dependency, not a third-party library, but it ties the
  implementation to the x86 compiler environment.
- `Vec3` operations are scalar. A future SIMD `Vec3` would require a separate
  aligned type rather than changing the packed public type.
- `TryInverse` is less ergonomic than an exception or a nullable return. It is
  the correct trade-off for a no-exception, allocation-free engine.
- `Transform` does not compose into another `Transform` when scale is
  non-uniform. Callers use `Mat4` for hierarchy composition, which is one extra
  step but mathematically honest.
- NaN payloads and exact NaN quieting are not preserved by `Half` conversion.
- The chosen Euler order is a convention; callers with another order must
  compose quaternions explicitly.

### Neutral

- `Mat4` can be uploaded directly to HLSL constant buffers that use
  `mul(M, v)`.
- The benchmark compares SSE2 against a scalar kernel on the development
  machine; results are evidence, not a portable guarantee.

## Alternatives considered

| Alternative | Why not |
|---|---|
| Row-major matrices | Contradicts ADR 0002 and HLSL default packing. |
| SIMD `Vec3` | Breaks packed vertex layouts or requires two public vector types. |
| Runtime CPU dispatch | Adds a branch or pointer to every operation before a second backend exists. |
| `std::optional<Mat4>` inverse | Adds an empty state and obscures the caller's failure branch. |
| `Result<Mat4, MathError>` inverse | Correct but noisier for a local numeric failure; `TryInverse` is simpler and allocation-free. |
| Inverted-infinite AABBs | Makes empty boxes indistinguishable from valid geometry. |
| Normal-Z projections | Worse float precision and contradicts the renderer convention. |
| Half arithmetic | Promoted to `f32` on mainstream CPUs; storage conversion is the useful boundary. |
| Full Euler extraction | Several valid orders; committing without a caller need invites silent bugs. |

## Follow-up

- Phase 4 may add allocator-aware containers; math stays allocation-free.
- Later rendering phases may add AVX2/NEON kernels behind `simd.h`.
- Any change to matrix layout, depth range, quaternion order, or Euler order
  requires a new ADR superseding this one and ADR 0002 where applicable.
