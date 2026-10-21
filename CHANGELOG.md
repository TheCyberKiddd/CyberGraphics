# Changelog

All notable changes to CyberGraphics are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
semantic versioning once a public API exists.

## [Unreleased]

### Added

- Repository skeleton: `engines/`, `include/`, `tools/`, `samples/`, `tests/`,
  `docs/`, `cmake/`, `scripts/`, `assets/`, and `third_party/`.
- Sixteen engine modules with written contracts in `engines/MODULES.md` and
  `engines/<module>/docs/MODULE.md`.
- Stable product contract location at `include/cybergraphics/`.
- CMake 3.28+ build with Debug, Release, Shipping, ASan, and CI presets.
- Module helper (`cyber_add_module`, `cyber_add_test`) and compiler policy.
- Structure and dependency-law checks (`scripts/check_structure.py`,
  `scripts/layer_check.py`).
- Windows and Linux CI workflow.
- Architecture, maths conventions, coding standard, ADR, testing, and
  performance documentation.
- Project requirements note and the Phase 1 design note, phase report, and
  ADRs 0001 (module architecture) and 0002 (math and depth conventions).
- `cyber_core` reference module with version metadata and a CTest smoke test.
- Install and CMake package export: `find_package(CyberGraphics CONFIG)` with
  `cyber::core` and `cyber::graphics_api` targets.
- `tests/integration/package_consumer` verifies the installed package on
  Windows and Linux CI.
- Single-source version generation from `project(VERSION)`.
- Explicit MSVC runtime selection (`/MD` by default, `/MT` option).
- Formatting gate (`scripts/check_format.py`), SHA-pinned GitHub Actions, and
  Dependabot updates.
- Integration guide, ADR 0003, and `SECURITY.md`.
- Phase 2 `cyber_core` primitives: fixed-width types, `BuildConfig`, `Error`,
  `Result<T, E>`, assertions, a bounded logger with sinks, and xxHash64.
- Project test runner in `tools/test` with `CG_TEST`, `CG_EXPECT_*`,
  `CG_REQUIRE`, listing, filtering, repeat, seed, shuffle, and console/TAP/JUnit
  output.
- ADR 0004, the Phase 2 design note, and the Phase 2 report.
- Phase 3 `cyber_math`: vectors, column-major matrices, quaternions, TRS
  authoring, right-handed reverse-Z view/projection matrices, AABB/plane/ray/
  sphere/frustum geometry, linear/sRGB color, and IEEE binary16 storage.
- Compile-time SSE2 `Vec4` fast path with a scalar fallback and SIMD-versus-
  scalar benchmark (`cyber_math_bench`).
- Absolute-tolerance test assertions: `CG_EXPECT_NEAR` and `CG_REQUIRE_NEAR`.
- ADR 0005, the Phase 3 design note, and the Phase 3 report.
- Phase 4 `cyber_memory`: the `Allocator` interface, `HeapAllocator`, linear,
  stack, pool, free-list, ring, frame, and tracking allocators, plus tagged leak
  reports and allocator accounting.
- `Span`, `StringView`, `String`, `DynamicArray`, `FixedArray`, open-addressing
  `HashMap`, `Handle<Tag>`, and generation-checked `SlotMap<T, Tag>`.
- Deterministic 128-block free-list stress coverage, deliberate-leak coverage,
  and stale-handle coverage in the memory tests.
- `cyber::memory` install/export and source/package consumer coverage.
- ADR 0006 and the Phase 4 design note.

### Notes

- The public API, renderer, RHI, and tools are not implemented yet. Planned
  modules intentionally have folders and contracts but no CMake targets.
- The project test runner is a developer tool; it is not installed and is never
  linked into a shipping target.
- `Transform` is an authoring record. Non-uniform TRS is not closed under
  composition, so hierarchies compose `ToMatrix()` values and invert through
  `TryInverse(Mat4&)`.
