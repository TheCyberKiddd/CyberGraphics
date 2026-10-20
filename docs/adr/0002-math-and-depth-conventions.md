# 0002 — Math and depth conventions

- **Status:** accepted
- **Date:** 2026-10-20
- **Deciders:** CyberEngine (owner)
- **Related:** [math conventions](../math-conventions.md), Phase 3

## Context

Every renderer eventually encodes a coordinate system, a matrix layout, and a
depth convention. If those choices are made implicitly in the first camera or
the first shader, they become impossible to change without rewriting importers,
projections, culling, shadow maps, and every golden image. The choices must be
made once, written down, and tested before the math library exists.

The project must import glTF assets, target D3D12 first, and later support
Vulkan and Metal. It also wants the precision of reverse-Z without a
backend-specific renderer.

## Decision

1. **Right-handed world.** `+Y` is up, `+X` is right, `+Z` points toward the
   viewer. The camera looks down `-Z`.
2. **glTF assets face `+Z`.** An unrotated glTF asset faces the camera; a
   character that should face away is rotated 180 degrees around `Y`. This
   matches the glTF 2.0 specification without changing the camera convention.
3. **Column vectors and column-major matrices.** `v' = M * v`;
   `m[column][row]`; 16 floats with column 0 first. Composition is
   `MVP = P * V * M`. HLSL uses `mul(M, v)` and receives matrices without a
   transpose.
4. **Clip space is 0..1 in depth with reverse-Z.** Near maps to 1, far maps to
   0, depth clears to 0, comparison is greater-or-equal. The infinite-far
   perspective variant uses `m22 = 0`, `m32 = near`, `m23 = -1`.
5. **NDC `+Y` is up in the RHI.** Vulkan's Y-down clip space is converted in the
   Vulkan backend, not in shared renderer code.
6. **Front faces are counter-clockwise** when viewed from the front. Back-face
   culling is the default for opaque geometry; backends set rasterizer state to
   match.
7. **Texture coordinates have `(0,0)` at the top-left**, with `V` increasing
   downward, matching glTF and image formats.
8. **Units are meters, seconds, kilograms, radians, and linear RGB.** Degrees and
   sRGB appear only at UI, serialization, or presentation boundaries.
9. **Quaternions are `(x, y, z, w)`**, identity `(0, 0, 0, 1)`, Hamilton product,
   `q1 * q2` applies `q2` first.
10. **Planes are `n · p + d = 0` with outward normals for frusta**; AABBs are
    inclusive; rays are `origin + t * direction` with `t >= 0`.

The full formulas, primitive rules, and test requirements are in
[math-conventions.md](../math-conventions.md).

## Consequences

### Positive

- glTF import is direct for orientation; only the "front faces the camera"
  interpretation needs a comment, not a conversion.
- D3D12 and Vulkan share the projection and depth logic; only the Vulkan backend
  handles its Y-down clip space.
- Reverse-Z gives usable precision across a large depth range on 32-bit float
  depth buffers.
- A single matrix layout avoids transposes and the `mul(M, v)` versus
  `mul(v, M)` class of bugs.
- Golden-image differences point to a convention bug first, which is easier to
  diagnose than a rendering bug.

### Negative

- The camera's forward axis is `-Z` while an asset's front is `+Z`; the
  distinction must be stated in the math doc and in importer comments.
- Vulkan needs an explicit Y flip that D3D12 does not.
- HLSL `row_major` declarations are forbidden unless a specification demands
  them, because they need a transpose.
- Changing any of these rules later requires an ADR and a migration of tests,
  shaders, and golden images.

### Neutral

- The software rasterizer must match the RHI's top-left fill rule and winding.
- Half floats are storage formats, not the default arithmetic format.

## Alternatives considered

| Alternative | Why not |
|---|---|
| Left-handed, `+Z` forward (D3D classic) | Awkward for glTF and for cross-platform math; more importer conversions. |
| Right-handed but camera looks down `+Z` | Contradicts the common RH camera and view-matrix derivation; confuses shadow and culling code. |
| Row vectors and row-major matrices | Would require transposes for HLSL's default packing and invites `mul(v, M)` bugs. |
| Depth `-1..1` (OpenGL) | D3D12, Vulkan, and Metal all use 0..1; the project targets those first. |
| Normal-Z depth | Simpler near/far mapping but much worse float precision over a large range. |
| `+Z` forward with `-X` right exactly as glTF axes | Correct formally, but makes screen-right negative in every camera and UI calculation. |
| Store degrees in runtime data | Forces conversions in hot paths and creates unit bugs. |

## Follow-up

- Phase 3 implements `Mat4`, `Vec3`, quaternions, and projection functions with
  tests for near/far corners, inverse, composition, and scalar/SIMD agreement.
- Phase 10 documents the RHI viewport and Y-flip boundary.
- Phase 14 proves the convention with the first triangle and a golden image.
- Any change to these rules requires a new ADR superseding this one.
