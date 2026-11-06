# Phase 23 — Geometry pipeline

**Status: Complete.** Mesh processing, instanced indirect submission, and the
rendered million-triangle/visible-LOD acceptance test are verified.

## What is implemented

- `cyber_scene::Mesh` validates and copies full-float vertices, 32-bit indices,
  contiguous material submeshes, and position-derived mesh/submesh bounds.
- Tangent generation orthogonalizes accumulated UV derivatives and stores
  handedness in tangent `w`; degenerate UVs use a deterministic fallback.
- Explicit packed conversion writes float positions, octahedral signed
  16-bit normals/tangents, half-float UVs, normalized 8-bit color, and
  quantized skin weights. Index packing selects 16 or 32 bits and checks every
  value and output range.
- The deterministic edge-collapse simplifier preserves submesh/material
  boundaries, rejects face inversions, and reports maximum referenced
  source-vertex displacement. This is not a full surface Hausdorff bound.
- Instance batch construction groups and sorts by mesh, material, LOD, and
  indexed range, producing 20-byte backend-neutral indirect indexed arguments
  and contiguous transforms.

Example:

```cpp
auto source = cyber::scene::Mesh::Create(meshDesc, allocator);
if (!source)
{
    return source.Error();
}

auto lod = cyber::scene::SimplifyMesh(source.Value(), targetTriangles, allocator);
if (!lod)
{
    return lod.Error();
}
```

See the [mesh API](../../engines/cyber_scene/include/cyber/scene/mesh.h), the
[design note](../design/phase-23-geometry.md), and
[ADR 0026](../adr/0026-geometry-ownership-and-draw-data.md).

## Million-triangle acceptance

- The integration test creates 10,000 instances of a 200-triangle source mesh,
  simplifies half of them, and checks that the selected LODs rasterize at least
  1,000,000 triangles.
- It copies the two 20-byte batch arguments into an RHI indirect buffer,
  records two indexed indirect draws, submits them through the null backend,
  and waits for the completion fence.
- The reference software renderer consumes the same batches and transforms.
  The test checks the rendered triangle and pixel counts and verifies the
  contrasting high-/low-detail regions.
- The 1600 × 1600 PPM capture is generated at
  `build/debug/phase23-lod-transition.ppm`. It is an ignored build output, not a
  committed asset.

Reproduce the full validation with `.\scripts\build.ps1 -Preset debug -Test`.
To regenerate only the image and acceptance evidence after building, run
`ctest --test-dir build\debug -R "^phase23_geometry$" --output-on-failure`.

## Validation

- Debug: 106/106 tests passed.
- Release: 106/106 tests passed.
- Debug-ASan: 106/106 tests passed.
- Installed Debug package consumer builds and runs with `cyber/scene/mesh.h`.
- Structure, layer, formatting, and whitespace checks pass.

The million-triangle image is produced by the deterministic software reference
renderer; the corresponding indirect commands are recorded and submitted on
the null RHI backend. No hardware-specific million-triangle benchmark or
D3D12 image comparison is claimed.

## Known limitation

The simplifier reports maximum source-vertex correspondence displacement. It
does not claim a maximum surface Hausdorff-distance bound.
