# ADR 0010: Depth, clipping, and perspective interpolation

- **Status:** accepted
- **Date:** 2026-10-24
- **Deciders:** @TheCyberKiddd
- **Related:** Phase 8, [design note](../design/phase-08-pipeline.md), ADR 0002,
  ADR 0009

## Context

Phase 7 rasterizes screen-space triangles with affine color and no visibility.
Phase 8 introduces the first real vertex path: transforms, a homogeneous near
clip, back-face culling, a depth buffer, and perspective-correct interpolation.
These choices decide every later backend's reference images, so they must be
written down before the code that depends on them exists.

The math conventions are already fixed by ADR 0002: right-handed, +Y up, column
vectors, column-major matrices, 0..1 depth with reverse-Z. The rasterization
conventions are fixed by ADR 0009: top-left origin, 24.8 fixed point, integer
edge functions, tile ownership. This record covers only what Phase 8 adds.

## Decision

1. **Clip space is the D3D convention.** `-w <= x <= w`, `-w <= y <= w`,
   `0 <= z <= w`. The near plane is `z = w`; the far plane is `z = 0`. Clipping
   happens in homogeneous clip space, before the perspective divide.
2. **Only the near plane is clipped.** The far plane is enforced by the
   reverse-Z depth test against a cleared-to-zero buffer (`z < 0` loses), and the
   side planes use a guard band. A triangle with any post-clip screen coordinate
   outside `+/-16384` pixels is dropped and counted as offscreen. That bound is
   the 24.8 fixed-point rasterizer limit.
3. **Depth is a float reverse-Z buffer.** It is cleared to 0, the test is
   greater-or-equal, and a passing sample writes its depth. NDC depth is
   interpolated barycentrically because `z_ndc` is affine in screen space.
4. **Attributes are interpolated perspective-correctly.** Each vertex carries
   `1 / w`; the rasterizer interpolates `attribute / w` and `1 / w` with the
   unbiased barycentric weights and divides once per covered pixel. The Phase 7
   affine path is kept byte-identical and does not use the divide.
5. **Winding is classified after the viewport y flip.** Positive screen-space
   signed area is clockwise. `FrontFace::Clockwise` is the default front face for
   right-handed object geometry, and culling happens after clipping and before
   binning. Zero area is always rejected.
6. **The viewport transform flips y and leaves depth in 0..1.**
   `screen.y = viewport.y + (0.5 - 0.5 * ndc.y) * height`. There is one viewport
   per draw; scissor rectangles are not part of Phase 8.
7. **Cameras expose plain values and delta-based controllers.** No platform
   input types cross into the renderer; the sample maps input to the controller
   structs. Pitch is clamped short of the poles, and orbit zoom is exponential
   in the wheel delta so distance never crosses zero.

## Consequences

### Positive

- Clip-space clipping removes the behind-camera wrap-around that a divide-first
  pipeline produces, and it is testable on its own because
  `ClipTriangleNearPlane` is public.
- The reverse-Z buffer with greater-or-equal matches D3D12 semantics, so the
  later GPU backend can reuse the same test and the same reference images.
- Perspective-correct color makes the perspective floor look right and removes
  the main visible limitation of Phase 7.
- Reusing the Phase 7 tile bins means culling, clipping, depth, and
  perspective interpolation cannot change coverage or determinism.
- Cameras without platform dependencies are unit-testable and reusable from the
  engine host in Phase 25.

### Negative

- Side-plane geometry outside the guard band is dropped rather than clipped. A
  camera very close to a large polygon can show a missing triangle at extreme
  angles. Six-plane clipping is the known fix.
- One floating-point division per covered pixel is the most expensive addition
  in the inner loop. The report records the cost at 1280x720.
- Coplanar surfaces still z-fight; greater-or-equal is deterministic but does
  not invent a depth bias.
- The depth target adds 4 bytes per pixel and must match the color target's
  dimensions in Phase 8.

### Neutral

- Index buffers and arbitrary varyings are not introduced here; Phase 9 adds
  them with the shader and OBJ work.
- The clipper returns a small fixed-capacity polygon instead of allocating.

## Alternatives considered

| Alternative | Why not |
|---|---|
| Divide first, clip in NDC | Geometry behind the camera wraps and covers the screen |
| Clip all six planes now | Larger clipper and more intersection cases; the build guide specifies near-plane clipping plus a guard band |
| 16-bit or 24-bit integer depth | Loses reverse-Z's near-plane precision |
| Greater test with a sentinel clear | Contradicts the roadmap's greater-or-equal and clear-0 requirement |
| Interpolate attributes affinely and divide the result | Wrong for foreshortened surfaces; the checkerboard floor shows it immediately |
| Put key codes in the camera API | Couples the renderer to platform input and makes tests need a window |
| Per-pixel reciprocal approximation | Not bit-identical across machines without a fixed polynomial; this is the reference renderer |

## Follow-up

- Phase 9 adds textures, shaders, and indexed meshes; the pipeline gains varying
  attributes and an index path but keeps this depth and clip convention.
- Phase 14's D3D12 backend must reproduce these images within tolerance using
  the same reverse-Z setup and greater-or-equal test.
- Six-plane clipping or a wider guard band is reconsidered when a scene large
  enough to show the gap exists.
