# Phase 18 report: material system

Date: 2026-11-06 · Commit: Phase 18 material system

## What was built

- Added immutable metallic-roughness material definitions and mutable
  instances, validated against `cyber_shaders` reflection.
- Added reflected parameter packing, texture-slot binding, upload staging and
  RHI copy/barrier recording, revision tracking, and deterministic draw queues.
- Added bounded `.cybermat` v1 parsing and serialization with UTF-8, numeric,
  record, and source-location validation.
- Added a D3D12 sample that renders 100 instances and exercises a live
  parameter update. The installed package now includes the `cyber_shaders` and
  `cyber_renderer` targets and public headers.

## Decisions

See [ADR 0021](../adr/0021-material-definitions-and-parameter-blocks.md).
The renderer reserves `b1, space2` for material constants, uses per-instance
uniform/staging buffers until Phase 19, and leaves texture URI resolution to a
later asset pipeline.

## Evidence for done-when

Machine: Windows 10 Pro 22H2, Intel Core i5-6400 (4 cores), Intel HD Graphics
530, driver `31.0.101.2125`.

| Command | Result |
|---|---|
| `.\scripts\build.ps1 -Preset debug -Test` | Passed, 98/98 tests. |
| `.\scripts\build.ps1 -Preset release -Test` | Passed, 98/98 tests. |
| `.\scripts\build.ps1 -Preset debug-asan -Test` | Passed, 98/98 tests. |
| `.\build\debug\samples\07_materials\cyber_sample_materials.exe --self-test` | Passed; 100 definitions, 100 instances, 100 sorted draws, live parameter change at frame 20. |
| `.\scripts\verify_package.ps1 -Preset release` | Passed; installed renderer package consumer built and ran. |
| `. .\scripts\bootstrap.ps1 -Quiet; cmake -S tests/integration/source_consumer -B build/source-consumer -G Ninja -DCMAKE_BUILD_TYPE=Debug; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; cmake --build build/source-consumer; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; & .\build\source-consumer\source_consumer.exe; exit $LASTEXITCODE` | Passed; source-tree renderer consumer built and ran. |
| `py -3 scripts/check_structure.py` | Passed. |
| `py -3 scripts/layer_check.py` | Passed. |
| `.\scripts\format.ps1 -Check` | Passed. |
| `git diff --check` | Passed. |

The sample's generated `material-report.txt` records
`live_parameter_change=passed` and `live_change_frame=20`.

## Known gaps

- Parameter upload uses one uniform and staging buffer per instance; Phase 19
  owns per-frame streaming and dynamic offsets.
- Callers must serialize updates against in-flight use and submit uploads
  before consuming draws.
- Texture URIs remain opaque; there is no resolver or asset loader in this
  phase.
- The sample was verified on the local Intel D3D12 adapter only. No
  cross-vendor hardware matrix or frame-capture comparison was performed.

## Sources used

- Khronos, [glTF 2.0 Materials](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials).
- Khronos, [glTF material schema](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/schema/material.schema.json).
- Google Filament, [Materials Guide](https://google.github.io/filament/Materials.html), conceptual reference only.

## What the next phase needs from this one

Phase 19 can preserve the material definition and instance semantics while
replacing per-instance uploads with aligned frame-ring streaming and adding
descriptor allocation and a tested bindless fallback.
