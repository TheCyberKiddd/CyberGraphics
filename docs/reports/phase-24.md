# Phase 24 report: asset import and cooked render assets

Date: 2026-11-07
Machine: Intel Core i5-6400, Intel HD Graphics 530, Windows 10 Pro, Visual Studio 2022/MSVC
Status: implementation and local validation complete

## What was built

- The `cyber_assets` module with bounded JSON, glTF 2.0 and OBJ import; in-house
  PNG/DEFLATE and baseline JPEG decoders; and DDS/KTX2 readers.
- Canonical mesh, image, texture, and metallic-roughness material types and
  validation.
- Versioned cooked mesh, texture, material, and scene containers with
  checksums, 16-byte-aligned sections, and zero-copy views.
- A JSON asset database with stable IDs, import settings, dependencies, and a
  cache that verifies cooked output before reuse.
- `cyber_cooker` and `AssetLibrary` hot reload with failed-reload preservation.
- A deterministic, bounded mutation corpus that exercises every in-memory
  parser and the cooked-file reader.
- A dynamically growing OBJ corner hash table. The Release benchmark exposed
  that the previous fixed-capacity table looped indefinitely on sufficiently
  large valid meshes; a 300-unique-vertex regression test now covers growth.

## Decisions

The module is CPU-only and depends on `cyber_core`, `cyber_math`,
`cyber_memory`, and `cyber_platform`; it does not depend on the RHI, renderer,
or jobs system. The asset database and cooked-file contracts are documented in
the [design note](../design/phase-24-assets.md) and
[cooked format specification](../formats-cooked.md).

## Evidence for done-when

| Exact command | Result |
|---|---|
| `.\scripts\build.ps1 -Preset debug -Test` | Passed; 122/122 CTest tests. |
| `.\scripts\build.ps1 -Preset release -Test` | Passed; 122/122 CTest tests. |
| `.\scripts\build.ps1 -Preset debug-asan -Test` | Passed; 122/122 CTest tests, including the bounded parser mutation test. |
| `Push-Location build\release\engines\cyber_assets; .\cyber_assets_bench.exe; Pop-Location` | Passed; 100-iteration glTF source import: 663.838 us/op; cooked scene file load: 186.492 us/op (3.56x); cooked mesh file load: 173.498 us/op (3.83x). |
| Same Release benchmark | The 16x16 OBJ source import took 295.100 us/op; in-memory cooked mesh parse took 3.140 us/op (93.98x). The small PNG source and cooked file paths measured 177.512 and 175.230 us/op respectively (1.01x). |
| `.\scripts\verify_package.ps1 -Preset debug` | Passed; installed-package consumer built and ran. |
| `py -3 scripts\check_structure.py` | Passed; 16 modules and one contract. |
| `py -3 scripts\layer_check.py` | Passed; 16 modules. |
| `clang-format --dry-run --Werror --style=file` on `engines\cyber_assets` and `tools\cooker` C++ files | Passed. |
| `.\scripts\format.ps1 -Check` | Reports three pre-existing violations in `engines\cyber_scene\src\mesh.cpp` and `engines\cyber_software_renderer\src\raster.cpp`; Phase 24 files pass. |
| `git diff --check` | Passed. |

The cooker acceptance tests verify that DDS and KTX2 sources with the same
stem produce distinct cooked paths, that all fixture formats cook, that the
manifest contains scene/image/derived-mesh relationships, and that a second
unchanged cook reuses validated cache entries. The benchmark and sanitizer
results above exercise the done-when criterion on this machine. The benchmark
uses the small `two_quads.gltf` fixture and measures a file-backed cooked load;
the result is evidence for this workload, not a cross-device performance
guarantee.

## Known gaps

- The importer is not a runtime streaming or GPU-upload system; those remain
  later renderer phases.
- Progressive JPEG, KTX2 supercompression, animation playback, skinning
  evaluation, and editor import UI are intentionally unsupported.
- The small PNG benchmark is near parity; it does not establish a general
  texture-loading speedup. A representative large-texture streaming benchmark
  belongs to later work.
- Linux sanitizer and broader CI results are not available from this Windows
  environment.

## Sources used

See the source list in the [Phase 24 design note](../design/phase-24-assets.md).

## What Phase 25 needs

Phase 25 can refer to stable path-derived asset IDs and load/inspect cooked
scenes and materials without inheriting source-parser dependencies. GPU upload
and renderer-facing lifetime policy remain outside this module.
