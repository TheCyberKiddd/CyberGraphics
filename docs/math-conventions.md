# Math conventions

These conventions are fixed before any math code is written. Changing them
later touches every matrix, shader, importer, camera, and test. If a convention
must change, write an ADR and migrate deliberately.

## Coordinate system

CyberGraphics uses a **right-handed** coordinate system:

```text
        +Y (up)
         |
         |
         +------ +X (right)
        /
      +Z (toward the viewer)
```

- **Up:** `+Y`.
- **Right:** `+X`.
- **Toward the viewer:** `+Z`.
- **Camera view direction:** `-Z`.
- **glTF assets:** the glTF 2.0 specification defines `+Y` up, `+Z` forward, and
  the front of an asset faces `+Z`. In our world that means an unrotated glTF
  asset faces the camera. To make a character face away, rotate 180 degrees
  around `Y`.
- **Positive rotation:** counter-clockwise when looking down the rotation axis
  toward the origin (right-hand rule).

## Vectors and matrices

- Vectors are **column vectors**.
- Matrices multiply on the left: `v_clip = M * v_world`.
- Composition applies right to left: `MVP = P * V * M`, so `P * V * M * v`
  applies `M`, then `V`, then `P`.
- A transform is `T * R * S` for scale, then rotate, then translate.
- Matrix storage is **column-major**: 16 floats, column 0 first, then column 1,
  column 2, column 3. The C++ accessor is `m[column][row]`.
- HLSL's default constant-buffer matrix packing is column-major, so a `Mat4`
  uploads without transposing when the shader uses `mul(M, v)`.
- Do not mix `mul(M, v)` and `mul(v, M)` in shaders. The project convention is
  `mul(M, v)`.
- Shaders that declare `row_major` require an explicit transpose at upload, or
  a different CPU layout. Avoid `row_major` unless a specification requires it.

## Clip space and depth

- Clip-space depth range is **0 to 1**.
- The renderer uses **reverse-Z**: the near plane maps to depth `1`, the far
  plane to depth `0`.
- The depth buffer is cleared to `0`.
- The depth comparison is `greater or equal`.
- Reverse-Z is used because floating-point precision is concentrated near zero;
  mapping the near plane to 1 preserves precision where it matters.
- The RHI accepts 0..1 depth for D3D12, Vulkan, and Metal. Any backend that
  differs converts at the boundary.
- NDC `Y` is **up** in the RHI's convention. A backend whose clip space has
  `Y` down (for example Vulkan) applies the flip in its viewport or projection
  conversion, never in shared renderer code.

### Right-handed perspective, reverse-Z, 0..1

With `f = 1 / tan(fovY / 2)` and `aspect = width / height`, the projection
matrix (column-major) is:

```text
[ f/aspect   0        0                      0 ]
[ 0          f        0                      0 ]
[ 0          0        near/(far-near)        -1 ]
[ 0          0        near*far/(far-near)    0 ]
```

Reading columns: `m00 = f/aspect`, `m11 = f`, `m22 = near/(far-near)`,
`m23 = -1`, `m32 = near*far/(far-near)`, `m33 = 0`.

The infinite-far variant is:

```text
m22 = 0
m32 = near
m23 = -1
```

Tests must project the near and far plane corners and assert `z_ndc == 1` and
`z_ndc == 0` respectively, including the infinite-far variant.

### Orthographic, reverse-Z, 0..1

```text
m22 = 1 / (far - near)
m32 = far / (far - near)
```

Near maps to 1, far maps to 0.

## Viewport and texture coordinates

- Viewport origin is top-left for API purposes.
- Framebuffer coordinates: `x` right, `y` down, pixel centers at `(x + 0.5,
  y + 0.5)`.
- Texture coordinates: `(0,0)` is the top-left of the image, `V` increases
  downward. This matches glTF and common image formats. Backends that sample
  with a different origin convert in the backend or the sampler description.
- Texel centers are at `(u * width - 0.5, v * height - 0.5)` for bilinear
  sampling. Nearest sampling uses `floor(u * width)` with clamping.
- Color textures are sRGB-encoded on disk and decoded to linear when sampled.
  Data textures (normal, roughness, metallic, masks) are linear.

## Winding and culling

- Front faces are **counter-clockwise** when viewed from the front in a
  right-handed system.
- Back-face culling is the default for opaque geometry.
- The RHI normalizes winding to the project convention; a backend with a
  different default sets its rasterizer state accordingly.
- The software rasterizer uses the same top-left fill rule as the RHI contract,
  so golden images are stable.

## Units and angles

- Length: meters.
- Time: seconds.
- Mass: kilograms.
- Angles: radians in code and runtime data; degrees only at UI and
  serialization boundaries.
- Color: linear RGB in lighting and compositing; sRGB only at presentation.
- Temperature: Kelvin for physical light sources, converted at the API edge.

## Quaternions

- Layout: `(x, y, z, w)`, with `w` the scalar part.
- Identity: `(0, 0, 0, 1)`.
- Rotation quaternions are unit length. Normalize after accumulation.
- Multiplication uses the Hamilton product. `q1 * q2` applies `q2` first, then
  `q1`, matching matrix composition.
- Rotating a vector: `v' = q * v * conjugate(q)` for unit `q`.
- Interpolation: normalized linear interpolation (nlerp) for small angles;
  spherical linear interpolation (slerp) when constant angular velocity matters.
  Choose one per use case and document it.

## Euler angles

- Authoring order is **YXZ**: yaw around `Y`, pitch around `X`, roll around `Z`.
- `FromEuler(yaw, pitch, roll)` builds `q = qY * qX * qZ`, so roll is applied
  first, then pitch, then yaw. This matches the public `Quat` header.
- Euler extraction is deliberately not part of the math API. Callers that need
  a different order compose axis-angle quaternions explicitly.
- Euler angles are radians, like every other angle in runtime code.

## Geometry primitives

- **AABB:** inclusive min/max. Empty boxes are represented explicitly, not by
  inverted infinities.
- **Plane:** `n · p + d = 0`, with `n` normalized. For frustum planes, `n`
  points outward; a point is inside when `n · p + d >= 0` for all six planes.
- **Ray:** `origin + t * direction`, `t >= 0`. Directions are normalized unless
  a function documents otherwise.
- **Frustum:** six planes extracted from the view-projection matrix, then
  normalized. Test AABBs with the positive-vertex method.

## Normals and tangents

- Normals transform by the inverse-transpose of the upper-left 3x3 of the model
  matrix, then normalize.
- Tangents transform by the model matrix, then orthogonalize against the normal.
- Bitangent handedness is stored as `w = ±1` and used as
  `bitangent = cross(normal, tangent) * w`.
- Normal maps are tangent-space, `+Y` up in tangent space, with the green
  channel not flipped unless an importer explicitly converts it.

## Half floats

- `half` is IEEE 754 binary16.
- Conversions must handle subnormals, infinities, NaNs, and signed zero.
- Half floats are storage formats, not the default compute format. Convert to
  float for arithmetic unless a measured path justifies otherwise.

## SIMD alignment

- `Vec4` and `Mat4` are 16-byte aligned.
- `Vec3` is 16-byte aligned in SIMD form; the scalar form is packed. The public
  `Vec3` is the packed form and SIMD kernels load it at the call site.
- The first fast path is SSE2, selected at compile time with a scalar fallback.
  There is no runtime dispatch until a second backend justifies it.
- Do not reinterpret a `float*` as a SIMD type. Use the provided load/store
  functions and respect alignment.
- The scalar fallback and SIMD paths must agree within the tolerance documented
  by the tests.

## Testing rules

- Every operation has a unit test with a tolerance helper.
- Test near/far projection, identity, inverse, composition, and degenerate
  cases.
- Test the scalar and SIMD paths against each other.
- Golden images use the same conventions; a mismatch is a convention bug first,
  a renderer bug second.

## Sources

- glTF 2.0 specification, coordinate system and units.
- "Depth Precision Visualized" (Nathan Reed), reverse-Z rationale.
- Scratchapixel, projection and rasterization stages.
- Real-Time Rendering, transforms and quaternion conventions.
- HLSL documentation, matrix packing order.
