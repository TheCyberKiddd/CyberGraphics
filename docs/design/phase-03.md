# Phase 3 — Math

Date: 2026-10-20
Status: implemented
Related: ADR 0002, ADR 0005,
[math conventions](../math-conventions.md)

## What this phase builds

`cyber_math` becomes the second active foundation module. It provides the
numeric types every later renderer module uses:

- scalar vectors (`Vec2`, `Vec3`, `Vec4`) and column-major matrices
  (`Mat3`, `Mat4`);
- unit quaternions and rigid/scale transforms;
- right-handed reverse-Z projection and view constructors;
- geometry primitives: inclusive AABB, plane, ray, sphere, and frustum;
- linear RGBA color and sRGB transfer functions;
- IEEE 754 binary16 storage conversion (`Half`);
- an SSE2 fast path for `Vec4`, with a scalar fallback for other targets.

The phase also adds absolute-tolerance assertions to the project test runner,
because every math operation is verified against a tolerance rather than exact
floating-point equality.

## What it deliberately leaves out

- AVX2, NEON, and runtime CPU dispatch. SSE2 is the first fast path; wider
  kernels can be added behind the same public API.
- Double-precision vector and matrix types. `f64` is used internally for
  inversion and determinant stability, not exposed as a type family.
- SIMD `Vec3`. The public `Vec3` stays packed so vertex layouts do not pay for
  padding; SIMD kernels load it into registers at the call site.
- Animation, scene graphs, curves, noise, and intersection acceleration
  structures. They belong to later modules and phases.
- Half-float arithmetic. `Half` is a storage conversion type; arithmetic stays
  in `f32`.
- Euler-angle extraction. Authoring supports YXZ Euler input, but matrix-to-
  Euler conversion is deliberately absent until a caller needs a documented
  order.
- A general matrix decomposition. `Transform` covers translation, unit
  quaternion rotation, and per-axis scale; shear and negative-scale policy are
  left to the caller.

## Interfaces it touches

Public headers added under `engines/cyber_math/include/cyber/math/`:

| Header | Responsibility |
|---|---|
| `constants.h` | Pi, epsilon, clamp/lerp/saturate, degree-radian conversion, approximate compare. |
| `types.h` | `cyber::math` aliases for the core fixed-width scalar types. |
| `simd.h` | Backend selection and the `SimdBackend` query. |
| `vec2.h` | Two-component scalar vector. |
| `vec3.h` | Packed three-component scalar vector; dot, cross, reflection. |
| `vec4.h` | 16-byte-aligned four-component vector with the SSE2 path. |
| `mat3.h` | Column-major 3x3 matrix and 3x3 inverse. |
| `mat4.h` | Column-major 4x4 matrix, transforms, view, and projections. |
| `quat.h` | Unit quaternion, rotation, interpolation, and Euler input. |
| `transform.h` | Translation/quaternion/scale authoring and exact matrix inverse. |
| `aabb.h` | Inclusive axis-aligned bounding box with an explicit empty state. |
| `plane.h` | Normalized plane, distance, and normalization. |
| `ray.h` | Ray, ray/AABB, and ray/sphere intersection. |
| `sphere.h` | Center/radius sphere. |
| `frustum.h` | Six-plane frustum extracted from a view-projection matrix. |
| `color.h` | Linear RGBA color and sRGB transfer functions. |
| `half.h` | IEEE binary16 storage type and round-trip conversion. |
| `math.h` | Umbrella include for callers that want the whole library. |

## Design

### Representation

`Vec3` is packed (`sizeof == 12`) so arrays and interleaved vertex data stay
compact. `Vec4` and `Mat4` are `alignas(16)` and have no hidden padding:
`Vec4` is 16 bytes and `Mat4` is 64 bytes. `Mat4` stores four `Vec4` columns,
matching the column-major `m[column][row]` convention and the HLSL constant
buffer layout.

`Mat3` stores three packed `Vec3` columns. It is used for the upper-left block
of a transform and for normal matrices; it is not padded to 16 bytes.

### SIMD

`simd.h` selects SSE2 when the compiler target provides it and otherwise
selects the scalar path. The selection is compile-time; there is no runtime
dispatch, function pointer, or initialization. `Vec4` arithmetic, dot product,
length, normalization, min/max, abs, and lerp use SSE2 when selected. Every
other type uses scalar code.

The public result is identical on both paths except for the documented
floating-point tolerance. Tests compare each SIMD operation against a
component-wise scalar reference. `SimdBackend()` is a diagnostic query, not a
switch that changes behavior at runtime.

### Matrices and inversion

Matrix multiplication composes right-to-left, as fixed by ADR 0002. A point is
transformed with `M * v` and homogeneous `w == 1`. `Mat4::TransformPoint`
performs the perspective divide when `w` is not one; `TransformVector` uses
`w == 0` and ignores translation.

Inversion is exposed as `bool TryInverse(Mat4& out) const` and
`bool TryInverse(Mat3& out) const`. A singular matrix is a recoverable,
caller-handled condition, so no exception, allocation, or global error state is
involved. The implementation uses `f64` Gaussian elimination with partial
pivoting and a relative pivot threshold (`1e-12 * maxAbs`). On failure the
output is left untouched.

Determinants use the same `f64` elimination path but no conditioning threshold:
only an exactly zero pivot collapses the result to zero, so a tiny but valid
determinant is still reported. The relative threshold belongs to `TryInverse`,
where it prevents an ill-conditioned inverse from being presented as usable.
Rotation constructors accept radians and use the right-hand rule. `Mat4::LookAt`
produces a right-handed view matrix with the camera looking down `-Z`.

### Projections

Projection constructors are named explicitly:

- `PerspectiveReverseZ(fovY, aspect, near, far)` — finite far plane;
- `PerspectiveInfiniteReverseZ(fovY, aspect, near)`;
- `OrthographicReverseZ(left, right, bottom, top, near, far)`.

All use 0..1 clip depth and map near to `1`, far to `0`. Tests project near and
far plane corners, including the infinite-far variant, and assert the NDC depth
values rather than only comparing matrix elements.

### Quaternions and transforms

Quaternions are `(x, y, z, w)` with `w` scalar, identity `(0, 0, 0, 1)`, and
Hamilton multiplication. `q1 * q2` applies `q2` first. Rotation assumes a unit
quaternion; `Normalize` and `FromAxisAngle` maintain that invariant.

Euler input is **YXZ**: yaw around Y, pitch around X, roll around Z, with
`q = qY * qX * qZ`. This applies roll first, then pitch, then yaw. The order is
documented here and in the public header because Euler order is otherwise
ambiguous.

`Transform` is translation plus unit rotation plus per-axis scale:
`p' = T + R * (S * p)`. It is an authoring record. A TRS transform with
non-uniform scale is not a closed algebra under composition: the product of two
TRS matrices is generally a rotation times a symmetric positive-definite scale,
not another rotation times a diagonal scale. The API therefore exposes
`ToMatrix()` for hierarchy composition and `TryInverse(Mat4&)` for the exact
inverse. It deliberately has no `Transform * Transform` operator, which would
silently produce wrong results for non-uniform scale.

### Geometry

- `Aabb` stores min/max inclusively and a separate `empty` flag. Empty is an
  explicit state, never inverted infinities.
- A plane is `n · p + d = 0`, with `n` normalized for geometry tests. Frustum
  planes point outward, so `n · p + d >= 0` means inside.
- A ray is `origin + t * direction`, with `t >= 0`; directions are normalized
  unless a function documents otherwise.
- Frustum extraction uses the view-projection rows. For 0..1 reverse-Z, the near
  plane is `row3 - row2` and the far plane is `row2`.
- AABB/frustum rejection uses the positive-vertex test. A straddling box is
  reported as intersecting; the test is conservative.

### Color and half floats

`Color` is linear RGBA. sRGB encode/decode uses the piecewise IEC 61966-2-1
transfer function and clamps RGB input to `[0, 1]`; alpha is untouched.

`Half` is IEEE binary16, 2 bytes, with round-to-nearest-even conversion,
subnormal support, signed zero, infinities, and NaN detection. NaN payloads are
canonicalized on conversion; the sign is preserved. The exhaustive test walks
all 65,536 bit patterns and round-trips every non-NaN value.

### Test runner

The runner gains `NearEquals`, `CG_EXPECT_NEAR`, and `CG_REQUIRE_NEAR`. Tolerance
is absolute and the expressions are evaluated once. NaN never compares near; a
negative tolerance fails.

## Alternatives considered

| Alternative | Why not now |
|---|---|
| Header-only scalar math | Simple, but it cannot demonstrate the required SIMD path and invites accidental layout changes. |
| SIMD `Vec3` | Saves a few instructions per operation but breaks packed vertex layouts or forces two public `Vec3` types. |
| Runtime SIMD dispatch | Adds a function pointer or branch to every operation before there is a second backend to justify it. AVX2 can be selected later at build time. |
| `std::optional` or `Result` for inversion | `TryInverse(out)` is branch-friendly, allocation-free, and leaves the output unchanged on failure. |
| Exceptions for singular matrices | Runtime code has exceptions disabled. |
| Row-major matrices | Contradicts ADR 0002 and HLSL's default constant-buffer packing. |
| Inverted-infinite AABBs | Makes empty boxes look like valid geometry and breaks naive min/max code. |
| Half arithmetic on the CPU | Modern CPUs promote to `f32` anyway; storage conversion is the useful contract. |
| Full Euler extraction | Several conventions are possible; shipping one without a documented order creates silent bugs. |

## Risks

- **SSE2 alignment:** `Vec4` loads use aligned SSE2 instructions. Every public
  `Vec4` and `Mat4` is aligned by the type system; unaligned external data must
  use `FromArray`. Tests and sanitizers cover the aligned path.
- **Inverse conditioning:** a relative pivot threshold rejects numerically
  singular matrices, but an ill-conditioned matrix can still produce large
  values. Callers must respect the returned success flag.
- **Quaternion drift:** repeated multiplication accumulates error.
  Rotation constructors normalize; long accumulation loops must call
  `Normalize`. `Transform::TryInverse` inverts the full matrix, so it remains
  exact even when the TRS representation is not closed under composition.
- **Half rounding:** the exhaustive 16-bit round-trip catches conversion
  mistakes, but NaN payloads are not preserved by design.
- **Frustum extraction:** the near/far row order depends on reverse-Z. Dedicated
  near/far corner tests prevent a normal-Z regression.
- **Benchmark noise:** the SIMD-versus-scalar report is indicative on one
  machine, not a portable performance guarantee.

## Test plan

1. Vector arithmetic, dot/cross, length, normalization, reflection, min/max,
   lerp, indexing, and zero-length behavior.
2. SIMD-versus-scalar component comparisons for every `Vec4` operation, plus
   alignment and backend assertions.
3. Matrix identity, composition, transpose, determinant, inverse, singular
   failure, and `Mat4` point/vector transforms.
4. Reverse-Z perspective (finite and infinite) and orthographic near/far
   corners, aspect, and `LookAt` orientation.
5. Quaternion axis-angle, composition order, matrix round-trip, normalization,
   nlerp/slerp, from-to, and YXZ Euler input.
6. Transform point/vector transform, matrix composition, exact matrix inverse
   round-trip, and zero scale rejection.
7. AABB empty/expand/contains/overlap, plane distance, ray/AABB and ray/sphere
   hits and misses, and frustum inside/outside/straddle cases.
8. sRGB transfer values, round-trip, clamping, and alpha preservation.
9. Half special values, subnormals, rounding, and exhaustive non-NaN round-trip.
10. Every public header included in one translation unit; each header also
    compiled by the module build.
11. All presets, Linux clang + UBSan, package/source consumers, and the
    SIMD-versus-scalar benchmark.

## Sources

- ISO C++20: `std::bit_cast`, `std::span`, `std::sqrt`, `std::pow`,
  `std::ldexp`, `std::signbit`.
- IEC 61966-2-1 sRGB transfer function.
- IEEE 754-2019 binary16 format and round-to-nearest-even rules.
- "Depth Precision Visualized" (Nathan Reed), reverse-Z rationale.
- DirectXMath and GLM documentation for API ergonomics and matrix conventions;
  no code copied.
- Real-Time Rendering, transforms and quaternion conventions.
- CyberGraphics ADR 0002 and math conventions.
