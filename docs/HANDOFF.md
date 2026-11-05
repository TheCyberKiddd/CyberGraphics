# Handoff — Phase 17

## Current state

- Phase 17 implementation and validation are complete and included in this
  commit.
- Baseline before Phase 17: `b9cb94318897287c3b714605b7a7acc57872f14a`.
- Debug, Profile, Release, Shipping, and Debug-ASan each pass 96/96 tests.
  The Profile package consumer, structure check, layer check, formatter, and
  `git diff --check` pass.
- The D3D12 live-reload self-test passes on the Intel HD Graphics 530:
  valid edit 48 ms, syntax-error rejection 49 ms (previous pipeline retained),
  restore 84 ms. The permutation report is generated.

## Decisions and known gaps

- Reflection is produced by `cyber_shaders`; `cyber_renderer` converts it to
  RHI layouts and publishes rebuilt pipelines only at frame boundaries.
- Permutations are limited to eight boolean keywords. Cache entries include
  compiler/source/permutation/target identity and validated checksums.
- File watching uses timestamp polling. The cache stores HLSL/reflection, not
  backend bytecode. Renderer layout building currently rejects storage-texture
  bindings and constant buffers other than the 128-byte `b0, space0` block.
- D3D12 live-reload evidence covers one Intel adapter, not a hardware matrix.
- The existing untracked `diagnosing-low-phase-6-sample-frame-rate.json` is
  unrelated and remains untouched.

## Files changed

- Shader compiler, cache, reflection, permutation, library, and tests:
  `engines/cyber_shaders/`.
- Renderer reflected layouts, hot pipelines, and tests:
  `engines/cyber_renderer/`.
- Empty RHI pipeline-layout support and regression test:
  `engines/cyber_rhi/`.
- Root/module/sample CMake wiring, `engines/modules.json`, and generated
  `engines/MODULES.md`.
- Phase 17 design, ADR 0020, reading notes, implementation summary, report,
  roadmap/status, changelog, README, and hot-reload sample.

## Exact next step

Read the Phase 18 material-system work package and write its design note before
starting implementation. Do not begin Phase 18 code until that design step is
complete.
