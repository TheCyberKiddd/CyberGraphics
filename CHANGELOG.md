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

### Notes

- The public API, renderer, RHI, and tools are not implemented yet. Planned
  modules intentionally have folders and contracts but no CMake targets.
