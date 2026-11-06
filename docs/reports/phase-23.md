# Phase 23 report: geometry pipeline

Date: 2026-11-06 · Status: complete

## What was built

- Added allocator-backed indexed mesh storage with full-float position, normal,
  tangent, two UV sets, color, joints, and weights.
- Added mesh/submesh validation and position-derived bounds.
- Added UV-derived tangent generation and explicit packed vertex/index output.
- Added deterministic material-boundary-preserving edge collapse with a
  maximum referenced source-vertex displacement metric.
- Added deterministic instanced draw batching with a backend-neutral 20-byte
  indexed-indirect argument layout.
- Added a cross-module acceptance test that uploads those arguments to an RHI
  indirect buffer, records and submits indexed indirect draws on the null
  backend, then renders the same LOD-selected batches with the software
  reference renderer.
- Rendered a 1600 × 1600 scene with 10,000 instances and at least 1,000,000
  selected triangles. The test writes a reproducible LOD-colored capture to
  `build/debug/phase23-lod-transition.ppm`.

## Decisions

See [ADR 0026](../adr/0026-geometry-ownership-and-draw-data.md), the
[design note](../design/phase-23-geometry.md), and
[reading notes](../notes/phase-23.md). CPU geometry and batch preparation stay
in `cyber_scene`; GPU resources, capability checks, and submission remain
renderer/RHI responsibilities.

## Evidence and validation

| Exact command | Result |
|---|---|
| `.\scripts\build.ps1 -Preset debug -Test` | Passed; 106/106 tests. |
| `.\scripts\build.ps1 -Preset release -Test` | Passed; 106/106 tests. |
| `.\scripts\build.ps1 -Preset debug-asan -Test` | Passed; 106/106 tests. |
| `ctest --test-dir build\debug -R "^phase23_geometry$" --output-on-failure` | Passed; the render/indirect-submission acceptance test passes and generates the PPM capture. |
| `.\scripts\verify_package.ps1 -Preset debug` | Passed; installed-package consumer compiles, links, and runs with `cyber/scene/mesh.h`. |
| `py -3 scripts\check_structure.py` | Passed; 16 modules and one contract. |
| `py -3 scripts\layer_check.py` | Passed; 16 modules. |
| `.\scripts\format.ps1 -Check` | Passed. |
| `git diff --check` | Passed. |

The acceptance scene contains 10,000 instances of a 200-triangle source mesh.
The test assigns 5,000 instances to each LOD, builds two deterministic batches,
copies the 20-byte draw arguments into a GPU-style indirect buffer, submits two
indexed indirect commands through the null backend, and verifies the reference
renderer consumed at least 1,000,000 triangles. The output shows red-tinted
high-detail instances on the left and blue-tinted simplified instances on the
right. The capture is reproducible and stays in the ignored build tree.

## Known limitations

- The reference render uses the CPU software renderer, and the RHI command
  submission uses its null backend; no hardware-specific million-triangle
  benchmark or D3D12 capture is claimed.
- Simplification reports maximum source-vertex correspondence displacement,
  not a maximum surface Hausdorff distance.

## Sources used

- Khronos, [glTF 2.0 specification — meshes](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes).
- Khronos, [glTF 2.0 geometry conventions](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#geometry).
- Microsoft, [Direct3D 12 indirect drawing](https://learn.microsoft.com/en-us/windows/win32/direct3d12/indirect-drawing).

## What Phase 24 receives

Phase 24 can import into the validated CPU `Mesh` contract and retain the
explicit packing and indirect-batch boundaries. The asset pipeline must not
move GPU resource ownership into `cyber_scene`.
