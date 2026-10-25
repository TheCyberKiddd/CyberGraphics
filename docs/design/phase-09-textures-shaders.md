# Phase 9 — Textures, CPU shaders, and test scenes

Date: 2026-10-25
Status: approved
Related: ADR 0002, ADR 0009, ADR 0010, ADR 0011,
[`cyber_software_renderer` contract](../engines/cyber_software_renderer/docs/MODULE.md),
[Phase 8 design note](phase-08-pipeline.md), [golden-image testing](../testing-golden-images.md)

## What this phase builds

Phase 9 turns the depth-tested pipeline into a programmable one and adds the
first real surface detail:

- **Textures.** A move-only texture with a box-filtered mip chain, the three
  wrap modes (repeat, clamp to edge, mirrored repeat), and nearest, bilinear,
  and trilinear sampling.
- **A CPU shader interface.** A vertex stage as a C++ function that maps an
  object-space `VertexInput` to a homogeneous clip position plus `Varyings`, and
  a pixel stage as a C++ function that maps interpolated `Varyings` to a linear
  color. The pipeline interpolates varyings perspective-correctly.
- **Lighting.** Lambert diffuse and Blinn-Phong specular, computed in linear
  space against a directional and a point light.
- **Loaders.** PPM and TGA readers, and an OBJ reader with positions, normals,
  UVs, and fan triangulation.
- **A golden-image harness.** Five fixed 640x360 scenes, a per-channel
  comparison, and a difference image on failure, all run in CI.
- **A visible sample.** `samples/04_textured` renders a textured, lit mesh.

The roadmap done-when line is: *a textured, lit model renders and five
golden-image tests pass in CI.*

## What it deliberately leaves out

- Normal mapping, parallax, and tangent spaces. Phase 18 introduces material
  channels; Phase 26 adds PBR.
- Shadows, ambient occlusion, and image-based lighting. Phases 28 and 29.
- Anisotropic filtering, texture arrays, and cubemaps. Phase 11.
- Compressed formats (BC, ASTC) and streaming. Phase 33.
- A shader language and runtime compilation. Phase 16 chooses that strategy;
  Phase 9 shaders are C++ function pointers.
- Indexed drawing inside `DrawTriangles`. Phase 9 adds an index buffer to the
  mesh type and expands it to a triangle list before the draw, keeping the
  Phase 8 pipeline unchanged.
- Alpha blending and sorting. A visible sample overwrites; transparency is a
  later feature.

## Interfaces it touches

### New public headers

| Header | Responsibility |
|---|---|
| `shading.h` | `VertexInput`, `Varyings`, `VertexStageOutput`, `VertexShaderFn`, `PixelShaderFn`, `Lerp(Varyings)`. |
| `texture.h` | `TextureFormat`, `AddressMode`, `FilterMode`, `SamplerDesc`, `TextureDesc`, `Texture`, `SampleTexture`. |
| `shader.h` | `ShaderDesc`, `DrawShadedTriangles`, `TransformVertexShader`, `TransformShaderData`. |
| `lighting.h` | `DirectionalLight`, `PointLight`, `SurfaceMaterial`, `LightingData`, `LambertBlinnPhongPixelShader`. |
| `mesh.h` | `MeshVertex`, `Mesh`, `ObjMesh`, `LoadObjFromMemory`, `LoadObjFile`. |
| `golden_image.h` | `GoldenImageResult`, `GoldenImageStatus`, `GoldenImageDesc`, `CompareImages`, `WriteDifferenceImage`. |

### Changed headers

| Header | Change |
|---|---|
| `raster.h` | Adds `ShadedRasterVertex`, `ShadedRasterTriangle`, `RasterizeShadedTriangles`. The Phase 7 and 8 types are unchanged. |
| `image_io.h` | Adds `ImageFormat` reading, `ReadImageFromMemory`, `LoadImage`. Writing is unchanged. |
| `software.h` | Umbrella includes the new headers. |

All changes are additive. Every Phase 6-8 caller compiles unchanged.

## Design

### Texture storage and mip chain

A texture owns one allocation for every level. Level `n` has dimensions
`max(1, width >> n)` by `max(1, height >> n)` and stops when both reach 1, so a
16384-texel edge produces 15 levels. Levels are tightly packed in order and
their offsets are computed once at creation.

`GenerateMips` box-filters each level from the one above: every destination
texel averages the 1 to 4 source texels it covers in linear space. Averaging the
linear values (rather than 8-bit codes) keeps a mip chain from darkening a
bright texture. Non-power-of-two dimensions average fewer samples on the odd
edge; that is recorded as a known limitation rather than fixed with a general
resampler.

Storage is `RGBA8Unorm` or `RGBA32F`, the same two linear formats as the
framebuffer, so texture data uploads to the CPU pipeline without conversion.

### Sampling

`SampleTexture(texture, u, v, desc)` runs one sampling operation:

1. Apply the wrap mode in normalized space: repeat is `frac`, clamp saturates to
   `[0, 1]`, and mirrored repeat folds with `1 - |frac|` on the odd half. NaN and
   infinity saturate.
2. Convert to texel space with `t = u * width - 0.5`. Nearest rounds; bilinear
   takes the floor and the fractional weight and fetches four texels, wrapping
   each fetch so the seam is continuous.
3. For a fractional level with a mip filter, sample the two adjacent levels and
   blend by the fractional part. This is the trilinear path; integer levels and
   a nearest mip filter fetch one level.

`SampleTextureLevel` exposes the level explicitly so tests and shaders can
choose a mip without derivatives. Phase 9 has no automatic LOD selection: a
pixel shader passes an explicit level, which keeps the reference renderer
deterministic.

### Shader interface

```text
VertexInput --vertex shader--> VertexStageOutput{ clipPosition, varyings }
clipPosition --near clip--> --/w--> NDC --viewport--> screen
Varyings --perspective-correct interpolation--> PixelShaderFn --> Color
```

- `VertexShaderFn` receives the object-space input and a caller `userData`
  pointer, and writes a `VertexStageOutput`. The built-in
  `TransformVertexShader` reads a `TransformShaderData` (model, view,
  projection, normal matrix, and a transform for normals and positions) so a
  simple scene needs no custom shader.
- `PixelShaderFn` receives the interpolated `Varyings` and the same `userData`,
  and returns linear RGBA.
- `Varyings` is a fixed struct: UV, world-space normal, world-space position,
  and vertex color. A fixed struct keeps the rasterizer's inner loop concrete
  and allocation-free; Phase 16 generalizes it when a shader language exists.
- Clipping interpolates the whole clip position and every `Varyings` field with
  the same factor as the position, so a clipped quad keeps correct UVs and
  normals.

`DrawShadedTriangles` mirrors `DrawTriangles`: validate targets, transform each
vertex through the caller's vertex shader, clip, cull, transform to screen, then
hand `ShadedRasterTriangle` records to `RasterizeShadedTriangles`. The shaded
rasterizer shares the Phase 7 coverage machinery (snapping, top-left rule, tile
binning) and replaces only the inner shading loop.

### Lighting

`LambertBlinnPhongPixelShader` reads `LightingData`:

```text
N = normalize(varying.normal)
V = normalize(eye - worldPosition)
for each directional light and point light:
    L = normalize(-direction) or normalize(position - worldPosition)
    atten = 1 / (1 + k1 * d + k2 * d^2) for point lights
    diffuse  += max(dot(N, L), 0) * light.color * material.albedo * (1 / pi)
    H = normalize(L + V)
    specular += pow(max(dot(N, H), 0), shininess) * light.color * material.specular
ambient = material.albedo * ambientColor
color   = ambient + diffuse + specular, times the texture sample when one is bound
```

The texture is sampled with the interpolated UV and multiplied into albedo, so a
scene can be textured and lit with the same shader. The result is linear; sRGB
encoding happens at present time.

### Loaders

- **PPM** reads the P6 (binary), P5 (binary gray), and P3 (ASCII) variants,
  skipping comments and clamping the maximum value to 255. P6 and P3 become
  RGBA8, P5 becomes gray RGBA8.
- **TGA** reads uncompressed true-color (type 2) and uncompressed gray (type 3)
  at 8, 24, or 32 bits per pixel, with either origin. Compressed and colormapped
  TGA is rejected with `Unsupported`.
- **OBJ** reads `v`, `vn`, and `vt` and face records of 1 to 3 indices. Faces
  with more than three vertices are fan-triangulated. Every index is
  range-checked, line length and record count are bounded by the buffer size,
  and a negative index is resolved relative to the end of the list. Missing
  normals or UVs get defaults; a malformed face fails the load.

All three parsers treat input as hostile: they never read past the span, never
recurse, and return `InvalidArgument` or `Unsupported` rather than asserting.

### Golden images

`CompareImages` walks both framebuffers, converts to 8-bit per channel, and
tracks the maximum absolute delta and the number of differing pixels. The result
is `Ok` inside the tolerance and `Mismatch` outside it. `WriteDifferenceImage`
writes the amplified absolute difference as an sRGB PPM for review. References
are committed under `engine*/tests/golden/`.

## Alternatives considered

- **A templated varying list.** More general, but it pushes templates through
  every raster entry point and changes the Phase 8 ABI. A fixed struct is enough
  for textures and lighting and is replaced in Phase 16.
- **A virtual shader interface.** Dispatch per vertex and per pixel through a
  vtable. Rejected: function pointers are cheaper and the module avoids virtual
  calls in hot loops.
- **Automatic LOD from derivatives.** Correct for a real GPU, but a CPU
  rasterizer has no quad derivatives and the result would depend on tile size.
  Explicit levels keep the golden images stable.
- **A general resampler for non-power-of-two mips.** Correct and more code;
  box filtering matches the build guide's "start with a box filter".
- **Storing textures as sRGB and decoding at sample time.** Decodes per sample
  and complicates the shader. The loaders convert once, keeping storage linear
  as the project rule requires.
- **A third-party OBJ or image library.** Forbidden by the project rules. Both
  parsers are small and are written from the specifications.
- **A per-test reference directory with PNG.** PPM is already written by Phase 6
  and needs no decompressor, so it is used at first as the build guide suggests.

## Risks

- A 16384-texel texture with a full mip chain is about 4x its base size in
  bytes; the mip chain is bounded and created in one allocation, so a failed
  allocation is reported rather than partially filled.
- Box filtering is not area-correct for non-power-of-two dimensions. Distant
  sampling of such a texture can show a slight seam; recorded as a known gap.
- Trilinear sampling with an explicit level cannot know the true LOD, so a
  shader that passes 0 always aliases. The sample passes a level derived from
  distance, and the limitation is recorded.
- The shaded pipeline carries `Varyings` (about 48 bytes) per vertex, so memory
  grows with the vertex count. At the test resolutions this is negligible.
- `f32` lighting with a small specular power can produce values above 1; the
  framebuffer clamps at quantization and the golden images are within range.
- The OBJ reader is deliberately strict; a file with unsupported records fails
  rather than silently dropping them. Assets must be clean.

## Test plan

`software_texture`: descriptor validation, mip chain dimensions, all three wrap
modes, nearest and bilinear sampling at known texel coordinates, trilinear
blending between two levels, RGBA8 and RGBA32F storage, out-of-range access, and
allocator balance.

`software_shader`: vertex-stage output, varying interpolation across a
perspective split, clipping preserving varyings, a constant pixel shader, and a
UV-pattern pixel shader producing the expected image.

`software_lighting`: Lambert falloff with the light angle, Blinn-Phong specular
peak, point-light attenuation, ambient term, and texture modulation.

`software_image_load`: PPM P6/P5/P3 with comments and a malformed header, TGA
24/32-bit both origins, and unsupported TGA rejection.

`software_mesh`: OBJ positions, normals, UVs, fan triangulation, negative
indices, and malformed faces.

`software_golden_image`: identical images pass, a one-channel change fails at
the right pixel, the diff image is written, and tolerance is honoured.

`software_golden_scenes`: five fixed 640x360 scenes render twice byte-identically
and match their committed references.

## Sources

- Fabian Giesen, "A trip through the Graphics Pipeline 2011", part 7 (sampling
  and mip selection).
- Microsoft, "Texture addressing modes" and the Direct3D sampler documentation.
- Lance Williams, "Pyramidal Parametrics", SIGGRAPH 1983 (mipmapping).
- James F. Blinn, "Models of Light Reflection for Computer Synthesized
  Pictures", SIGGRAPH 1977 (Blinn-Phong).
- Johann Heinrich Lambert, *Photometria*, 1760 (Lambert diffuse).
- Scratchapixel, "Textures", "Normal Mapping", and "Lighting" chapters.
- Wavefront, "OBJ File Format Specification" (face and index rules).
- CyberGraphics ADR 0002, 0009, 0010, and 0011, and the Phase 8 design note.
