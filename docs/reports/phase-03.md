# Phase 3 report: math library

Date: 2026-10-21
Commits: `016e6c7..f98d602` (math implementation, tests, package integration,
documentation, and the phase report; `14ead49` adds the agent rulebook and
roadmap index)
Machine: Windows 10 Pro 19045, Intel Core i5-6400, 16 GB RAM, Intel HD 530,
MSVC 19.44.35225, CMake 3.31.6, Ninja preset builds

## What was built

`cyber_math` is now the second active foundation module:

- `constants.h`, `types.h`, and `simd.h` for scalar helpers, aliases, and the
  compile-time SSE2 backend query.
- `Vec2`, `Vec3`, and `Vec4`; `Mat3` and column-major `Mat4`.
- Unit quaternions with Hamilton multiplication, axis-angle construction,
  matrix round-trip, nlerp/slerp, and YXZ Euler input.
- `Transform` as translation, unit quaternion rotation, and per-axis scale
  authoring.
- Right-handed, reverse-Z finite/infinite perspective and orthographic
  projections, plus `LookAt`.
- Inclusive empty-aware `Aabb`, normalized `Plane`, `Ray` intersections,
  `Sphere`, and six-plane `Frustum`.
- Linear RGBA `Color` with IEC 61966-2-1 sRGB transfer functions.
- IEEE binary16 `Half` storage conversion with round-to-nearest-even,
  subnormals, infinities, signed zero, and canonicalized NaN.
- A compile-time SSE2 `Vec4` fast path with a scalar fallback.
- `cyber_math_bench`, comparing SIMD and scalar `Vec4` addition.
- `CG_EXPECT_NEAR` and `CG_REQUIRE_NEAR` in the project test runner.

The module is installed and exported as `cyber::math`. The source and installed
package consumers link it and exercise vectors, matrices, quaternions, reverse-Z
projection, half floats, and AABBs.

Example:

```cpp
#include <cyber/math/math.h>

using namespace cyber::math;

const Mat4 model = Mat4::Translation({1.0f, 2.0f, 3.0f}) *
                   Mat4::RotationZ(kHalfPi) * Mat4::Scale({2.0f, 2.0f, 2.0f});
const Mat4 view = Mat4::LookAt({0.0f, 0.0f, 5.0f}, Vec3::Zero(),
                               {0.0f, 1.0f, 0.0f});
const Mat4 projection = Mat4::PerspectiveReverseZ(kHalfPi, 16.0f / 9.0f, 0.1f, 100.0f);

Mat4 inverse;
if (!model.TryInverse(inverse))
{
    // The matrix is singular or numerically unusable; handle it explicitly.
}
```

## Decisions

- [ADR 0005](../adr/0005-math-representation-and-simd.md): packed `Vec3`,
  aligned `Vec4`/`Mat4`, compile-time SSE2 selection, `TryInverse(out)`, explicit
  reverse-Z projection names, quaternion conventions, inclusive empty AABBs, and
  `Half` as binary16 storage.
- [Phase 3 design note](../design/phase-03.md): alternatives, risks, and test
  plan.
- `Transform` deliberately has no `Transform * Transform` operator. Non-uniform
  TRS is not closed under composition; hierarchies use `ToMatrix()` and exact
  inversion uses `TryInverse(Mat4&)`.
- `Mat4::Determinant` uses `f64` elimination without the inverse conditioning
  threshold. Only an exactly zero pivot reports zero. `TryInverse` keeps the
  relative `1e-12 * maxAbs` threshold.
- The test runner's tolerance is absolute. A negative tolerance fails; NaN never
  compares near.

## Evidence for done-when

The Phase 3 done-when is: every operation has unit tests, the projection test
passes, and the benchmark result is saved in the phase report.

### Structure, dependency law, and formatting

```text
python scripts/check_structure.py
Structure check passed: 16 modules, 1 contract(s).

python scripts/layer_check.py
Layer check passed: 16 modules.

scripts\format.ps1 -Check
(no output; exit 0)
```

### Test presets

All 17 CTest entries pass in every preset:

| Preset | Result | CTest |
|---|---|---|
| `debug` | build ok | 17/17 passed |
| `release` | build ok | 17/17 passed |
| `shipping` | build ok | 17/17 passed |
| `debug-asan` | build ok | 17/17 passed |

CTest entries:

```text
test_runner, core_result, core_assert, core_log, core_hash,
core_build_config, core_version, core_public_headers,
math_vec, math_mat, math_projection, math_quat, math_transform,
math_geometry, math_color, math_half, math_public_headers
```

### Benchmark

`cyber_math_bench`, 20,000,000 iterations, MSVC x64:

| Build | SIMD | Scalar | Speed-up |
|---|---:|---:|---:|
| release | 1.37 ns/op (27.472 ms) | 3.16 ns/op (63.209 ms) | 2.31x |
| shipping | 1.42 ns/op (28.345 ms) | 3.26 ns/op (65.145 ms) | 2.30x |

The benchmark reports `backend=sse2`. These values are indicative on the
development machine, not a portable performance guarantee.

### Integration

```text
scripts\verify_package.ps1 -Preset debug
install + package consumer configure, build, run: exit 0

scripts\verify_package.ps1 -Preset release
install + package consumer configure, build, run: exit 0

build\source-consumer\source_consumer.exe
exit 0
```

The package consumer exercises `cyber::math` through the installed CMake
package, and the source consumer exercises the same code through
`add_subdirectory`.

### CI

GitHub Actions run [39580848319](https://github.com/TheCyberKiddd/CyberGraphics/actions/runs/39580848319)
passed for `f98d602`:

| Job | Result |
|---|---|
| Structure and dependency law | pass |
| Formatting | pass |
| Windows MSVC (configure, build, test, install, consumers) | pass |
| Linux clang + UBSan (configure, build, test, package, consumers) | pass |

The run completed all four jobs successfully. The only annotation was the
upstream `ilammy/msvc-dev-cmd` Node 20 deprecation notice; it does not affect
the build or tests.

## Known gaps

- SIMD is SSE2 only. AVX2, NEON, and runtime CPU dispatch are deliberately out
  of scope until measurements justify them.
- The benchmark measures `Vec4` addition only; it is not a full operation suite.
- `Transform` has no multiplication operator and no full matrix decomposition by
  design. Non-uniform TRS hierarchies must compose matrices.
- Euler extraction is absent; only YXZ Euler input is supported.
- `Half` is storage-only; arithmetic stays in `f32`.
- Local `clang-format` is 19.1.5 while the Ubuntu CI image may use 18.x. The local
  formatting check passes; CI formatting remains the authoritative check.
- Branch protection is still a GitHub repository setting and is not encoded in
  the repository.
- The stable C ABI remains empty until Phase 25; `cyber_math` is a C++ module
  interface, not a shipping ABI.

## Sources used

- ISO C++20: `std::bit_cast`, `std::span`, `std::sqrt`, `std::pow`,
  `std::ldexp`, `std::signbit`.
- IEC 61966-2-1 sRGB transfer function.
- IEEE 754-2019 binary16 format and round-to-nearest-even rules.
- "Depth Precision Visualized" (Nathan Reed), reverse-Z rationale.
- DirectXMath and GLM documentation for API ergonomics and matrix conventions;
  no code copied.
- Real-Time Rendering, transforms and quaternion conventions.
- CyberGraphics ADR 0002, ADR 0005, and the math conventions.

## What the next phase needs from this one

Phase 4 can rely on:

- `cyber::math` as a stable C++ module target with self-contained public headers.
- The layout rules in `math-conventions.md`: right-handed, +Y up, column vectors,
  column-major matrices, 0..1 reverse-Z, radians, meters, linear RGB.
- `Vec3` at 12 bytes, `Vec4`/`Mat4` at 16-byte alignment, and no heap allocation
  in math operations.
- `TryInverse(out)` as the fallible inverse convention and `Result`/`Status` as
  the recoverable-error convention from `cyber_core`.
- The test runner's absolute-tolerance helpers for future floating-point tests.

Phase 4 should add `cyber_memory` containers and generation-counted handles
without changing the math public API.
