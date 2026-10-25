# Phase 9 report — Textures, CPU shaders, and test scenes

Date: 2026-10-25
Status: implemented; all 51 tests pass in every local preset, CI re-run in flight
Design note: [phase-09-textures-shaders.md](../design/phase-09-textures-shaders.md)
ADR: [0011](../adr/0011-textures-shaders-and-golden-images.md)
Contract: [`cyber_software_renderer`](../../engines/cyber_software_renderer/docs/MODULE.md)

## Done-when line

> A textured, lit model renders and five golden-image tests pass in CI.

Both halves are proved locally. Five golden scenes render a textured cube, a
Lambert cube, a Blinn-Phong cube, a UV gradient, and a depth-ordered pair of
quads at 640x360, and each is compared with a committed reference. CI runs the
same test in every preset; the run link is added when the branch is pushed.

## What was built

New public headers under `include/cyber/software/`:

| Header | Contents |
|---|---|
| `shading.h` | `VertexInput`, `Varyings`, `VertexStageOutput`, `VertexShaderFn`, `PixelShaderFn`, `LerpVaryings`. |
| `texture.h` | `TextureFormat`, `AddressMode`, `FilterMode`, `SamplerDesc`, `TextureDesc`, `Texture`, `SampleTexture`, `SampleTextureLevel`. |
| `shader.h` | `TransformShaderData`, `TransformVertexShader`, `ShaderDesc`, `DrawShadedTriangles`. |
| `lighting.h` | `DirectionalLight`, `PointLight`, `SurfaceMaterial`, `LightingData`, `LambertBlinnPhongPixelShader`. |
| `mesh.h` | `Mesh`, `LoadObjFromMemory`, `LoadObjFile`. |
| `golden_image.h` | `GoldenImageStatus`, `GoldenImageDesc`, `GoldenImageResult`, `CompareImages`, `WriteDifferenceImage`. |

Changed headers: `raster.h` adds `ShadedRasterVertex`, `ShadedRasterTriangle`,
and `RasterizeShadedTriangles`; `image_io.h` adds `ReadImageFromMemory` and
`LoadImage`; `software.h` includes the new headers. Every Phase 6-8 caller still
compiles unchanged.

New source files: `texture.cpp`, `shader.cpp`, `lighting.cpp`, `mesh.cpp`,
`golden_image.cpp`; `raster.cpp` and `image_io.cpp` gained the shaded and reading
paths.

### Textures

`Texture` owns one allocation for the whole mip chain. Levels are box filtered
in linear space and stop at 1x1. Sampling applies repeat, clamp-to-edge, or
mirrored repeat in normalized space, then nearest or bilinear inside a level,
and blends two levels for trilinear when the mip filter is bilinear. Storage is
`RGBA8Unorm` or `RGBA32F`, both linear.

### Shaders

A vertex stage maps an object-space `VertexInput` to a clip position plus
`Varyings`; a pixel stage maps interpolated `Varyings` to a linear color. The
shaded rasterizer shares the Phase 7 coverage machinery (24.8 fixed point,
top-left rule, tile bins) and replaces only the inner shading loop. Varyings are
interpolated through `attribute / w` over `1 / w`, and the near clipper
interpolates every field with the position factor.

### Lighting

`LambertBlinnPhongPixelShader` implements Lambert diffuse with a `1/pi`
normalization and Blinn-Phong specular, against up to two directional and four
point lights with smoothed range falloff. Albedo is modulated by the vertex
color and, when bound, a sampled texel.

### Loaders

PPM reads P6, P5, and P3 with comments and a 255 maximum. TGA reads uncompressed
true-color (24/32-bit) and gray (8-bit) in both origins. OBJ reads `v`, `vn`,
`vt`, and faces with one to three indices, fan-triangulating polygons and
resolving negative indices. All three treat input as hostile: bounded records,
range-checked indices, and explicit `InvalidArgument`/`Unsupported` failures.

Decoded images are converted from sRGB to linear at load time and returned as
`RGBA32F`, so lighting math stays linear and no precision is lost to 8-bit
storage.

## API example

```cpp
TransformShaderData transform;
transform.modelViewProjection = projection * view * model;
transform.modelToWorld = model;
transform.normalToWorld = model;
transform.eyePosition = eye;

LightingData lighting;
lighting.eyePosition = eye;
lighting.texture = &checker;
lighting.material.albedo = Color{1.0f, 1.0f, 1.0f, 1.0f};
lighting.directional[0].direction = Vec3{-0.4f, -1.0f, -0.6f};
lighting.directionalCount = 1;

ShaderDesc desc;
desc.pipeline.viewport = Viewport{0.0f, 0.0f, 640.0f, 360.0f};
desc.vertexShader = &TransformVertexShader;
desc.pixelShader = &LambertBlinnPhongPixelShader;
desc.userData = &scene;  // a struct holding transform and lighting

cyber::software::DrawShadedTriangles(desc, mesh.vertices, color, depth, &jobs, heap, &stats);
```

The sample wraps that in a `SceneUniforms` struct and a two-line vertex shim so
the vertex stage can read the transform while the pixel stage reads the light
data.

## Evidence

All commands ran on Windows 11, 13th Gen Intel Core i7, MSVC 19.44, CMake 3.31.

| Check | Result |
|---|---|
| `scripts/build.ps1 -Preset debug` | pass |
| `scripts/build.ps1 -Preset release` | pass |
| `scripts/build.ps1 -Preset shipping` | pass |
| `scripts/build.ps1 -Preset debug-asan` | pass |
| `ctest` debug / release / shipping / debug-asan | **51/51 each** |
| `scripts/check_structure.py` | 16 modules, 1 contract |
| `scripts/layer_check.py` | 16 modules |
| `scripts/check_format.py` | 197 files |
| `scripts/verify_package.ps1` | installed and source consumers build and run |
| `cyber_sample_textured --capture` | 6 submitted, 6 culled, 30709 pixels; two writes byte-identical |

New tests, all passing in all four presets:

- `software_texture` — 7 cases: descriptor and mip dimensions, texel round trip,
  upload and box average, all three wrap modes, nearest/bilinear, trilinear,
  allocator balance.
- `software_shader` — 4 cases: constant color over a quad, varying
  interpolation, near-plane clipping, invalid arguments.
- `software_lighting` — 5 cases: Lambert falloff, point attenuation, Blinn-Phong
  peak, texture modulation, vertex tint.
- `software_image_load` — 5 cases: P6, P3 with comments, TGA, rejection paths,
  encode/decode round trip.
- `software_mesh` — 4 cases: triangle, fan triangulation, negative indices,
  malformed input.
- `software_golden_image` — 1 case: match, mismatch with one pixel, tolerance,
  dimension mismatch, difference image written.
- `software_golden_scenes` — 6 cases: five scenes plus a determinism check.

### Golden references

| Scene | File |
|---|---|
| Textured cube | `engines/cyber_software_renderer/tests/golden/checker_cube.ppm` |
| Lambert cube | `engines/cyber_software_renderer/tests/golden/lambert_cube.ppm` |
| Blinn-Phong cube | `engines/cyber_software_renderer/tests/golden/blinn_cube.ppm` |
| UV gradient | `engines/cyber_software_renderer/tests/golden/uv_gradient.ppm` |
| Depth order | `engines/cyber_software_renderer/tests/golden/depth_order.ppm` |

Each is 640x360 P6 (691,215 bytes). All five reproduce byte-identically on a
second render in the same run.

### Visible sample

`cyber_sample_textured` renders a rotating checker-textured cube with Lambert
diffuse and a Blinn-Phong highlight. Capture: `6 submitted, 6 culled` — the
back faces never reach the rasterizer. The image is committed at
`docs/reports/images/phase9_textured.png`.

The release capture (640x360, texture generation, mip chain, shaded raster, PPM
encode) completes in 0.14 s warm and 0.22 s on the first run, measured over
three consecutive captures; it is a deterministic function of the fixed camera.

## Known issues and gaps

- **No automatic LOD.** A pixel shader passes an explicit mip level; a caller
  that passes 0 always aliases. The scenes use level 0 with a nearest filter, so
  this is not visible, but it is a real limitation.
- **Box filtering is not area-correct for non-power-of-two dimensions.** Odd
  edges average fewer samples.
- **`Varyings` is fixed.** Adding an attribute changes a public struct; Phase 16
  generalizes it with a shader language.
- **No alpha blending.** A covered sample overwrites.
- **OBJ is strict.** Unsupported records are ignored but a malformed face fails
  the load; there is no material (`mtllib`) support.
- **The parsers are not fuzzed.** They are bounded and range-checked; fuzzing is
  Phase 40 work.
- **The first CI run failed twice and both causes are fixed.** Clang rejected an
  unused constant in the new sample (`-Wunused-const-variable`, which MSVC does
  not emit), and the source-consumer build failed to configure because the
  golden-directory definition was applied to a test target that only exists in
  the CyberGraphics build. The definition is now guarded with `if(TARGET ...)`,
  and the consumer configures, builds, and runs locally.

## Sources

See the design note and `docs/notes/phase-09.md`. Primary references:
Pineda 1988 (coverage), Giesen's pipeline series part 7 (sampling), Microsoft
texture addressing and sampler documentation, Williams 1983 (mipmapping), Blinn
1977 (specular), Lambert's *Photometria*, the Wavefront OBJ specification, and
CyberGraphics ADRs 0002, 0009, 0010, and 0011.
