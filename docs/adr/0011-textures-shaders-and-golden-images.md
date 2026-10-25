# 0011 — Texture sampling, CPU shader interface, and golden images

- **Status:** accepted
- **Date:** 2026-10-25
- **Deciders:** @TheCyberKiddd
- **Related:** Phase 9, [design note](../design/phase-09-textures-shaders.md),
  [golden-image testing](../testing-golden-images.md), ADR 0002, ADR 0009, ADR 0010

## Context

Phase 8 renders flat-colored, depth-tested geometry. Phase 9 must render a
textured, lit model, which needs three things the pipeline does not have:
surface detail from textures, a way to compute a color per pixel instead of
interpolating one, and a regression net that proves the image did not change.

The project rules constrain the options. No third-party library may provide a
sampler, a shader system, or a file loader. Runtime code has no exceptions and
no RTTI, so a per-pixel dispatch cannot rely on `dynamic_cast` or a throwing
API. Shaders are C++ functions until Phase 16 chooses a shader language.

## Decision

1. **Textures own a box-filtered mip chain and sample in normalized UV space.**
   `SampleTexture` applies the wrap mode, fetches nearest or bilinear inside a
   level, and blends two levels for trilinear. Storage is linear `RGBA8Unorm`
   or `RGBA32F`, matching the framebuffer.
2. **Shaders are function pointers over fixed structs.** A vertex stage maps a
   `VertexInput` to a clip position plus a fixed `Varyings`, and a pixel stage
   maps interpolated `Varyings` to a color. `Varyings` carries UV, world normal,
   world position, and vertex color.
3. **Interpolation is perspective-correct for every varying**, using the Phase 8
   `attribute / w` over `1 / w` rule, and clipping interpolates the same fields.
4. **Loaders and golden images are written from the specifications.** PPM, TGA,
   and OBJ readers treat input as hostile. Golden scenes render at a fixed
   640x360 and compare with a per-channel tolerance.
5. **The Phase 6-8 interfaces do not change.** The shaded path is additive; the
   old types and entry points keep their arithmetic and output.

## Consequences

### Positive

- A textured, lit model renders with no external dependency.
- The shader interface is a plain C++ call, so tests can supply a constant
  shader and a golden scene can be reasoned about without a compiler.
- Golden images become the regression net for every later phase.
- Linear storage plus load-time sRGB decoding keeps lighting math correct.

### Negative

- `Varyings` is fixed. A new attribute changes a public struct, which is a
  source-compatible but not ABI-compatible change in Phase 9; Phase 16
  generalizes it deliberately.
- There is no automatic mip selection. A shader passes an explicit level, so an
  unaware caller aliases.
- Box filtering is not area-correct for non-power-of-two dimensions.
- Two new parsers are attack surface. They are bounded and range-checked but
  need fuzzing in Phase 40.

### Neutral

- TGA and PPM are the only image inputs; PNG waits for Phase 24.
- OBJ is the only mesh input; glTF waits for Phase 24.

## Alternatives considered

| Alternative | Why not |
|---|---|
| A virtual `IShader` interface | Per-pixel virtual dispatch and an object model the module does not need; function pointers are cheaper and simpler. |
| A templated varying list | Pushes templates through every raster entry point and changes the Phase 8 ABI; deferred to Phase 16. |
| Automatic LOD from derivatives | A CPU rasterizer has no quad derivatives; a tile-size-dependent LOD would break golden-image stability. |
| sRGB textures decoded at sample time | Decodes per sample and moves transfer-function logic into the shader; load-time conversion is simpler and matches the linear-storage rule. |
| A third-party OBJ/image library | Forbidden by the project rules and unnecessary for the formats Phase 9 needs. |
| PNG golden images | Needs DEFLATE now; PPM is already written by Phase 6 and diffs trivially. |

## Follow-up

Implement the types and functions above, add the five golden scenes and their
committed references, and record the sample capture in the Phase 9 report.
Deferred: shader language and reflection (16), material channels (18), shadows
(28), PBR (26), texture compression and streaming (33), fuzzing the parsers (40).
