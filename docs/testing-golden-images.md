# Golden-image testing

A golden-image test renders a fixed scene and compares the result with a stored
reference image. It catches the class of regression that unit tests on
individual functions miss: a correct clipper feeding a rasterizer with a subtly
wrong sign, an interpolation that drifts one ulp per pixel, or a sampler that
wraps one texel early.

## Rules

- **The scene is code.** Camera, geometry, lights, and texture data are
  constructed in the test, not loaded from a file, so the input cannot drift.
- **The seed is fixed.** No randomness reaches a golden scene. Where a pattern
  needs variation it uses an arithmetic formula over the pixel index.
- **The resolution is fixed.** Golden scenes render at 640 by 360. A different
  size is a different golden image.
- **The image is deterministic.** Rendering the same scene twice in one process
  and across processes produces identical bytes. The `software_golden_scenes`
  test asserts this before comparing.
- **The reference is committed.** References live in
  `engines/cyber_software_renderer/tests/golden/` as PPM. They are small, plain,
  and reviewed like source.
- **Comparison uses a tolerance.** Each channel is compared with a default
  absolute tolerance of `2 / 255` in 8-bit space, configurable per call. A
  fully deterministic renderer normally differs by zero; the tolerance absorbs
  compiler and math-library differences across machines.

## Comparison and failure output

`CompareImages` returns a `GoldenImageResult`:

- `maxChannelDelta` in 8-bit units and `differingPixels`;
- `Ok` when every channel is within tolerance;
- `Mismatch` otherwise.

On mismatch the harness writes a difference image next to the failing reference:
each pixel is the absolute per-channel difference, amplified so a small drift is
visible. The test prints the reference path, the difference path, and the worst
channel so the failure is actionable from CI logs alone.

## CI wiring

`software_golden_scenes` is a `cyber_add_test` target and therefore runs in every
preset, including the CI jobs. It regenerates the five scenes into a temporary
allocator-backed buffer and compares against the committed references. The
`--update` switch is deliberately not wired into CI: references are updated by a
developer, reviewed as an image diff, and committed.
