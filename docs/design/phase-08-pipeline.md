# Phase 8 — Depth, clipping, and perspective

Date: 2026-10-24
Status: approved
Related: ADR 0002, ADR 0009, ADR 0010, `cyber_software_renderer` contract,
[Phase 7 design note](phase-07-raster.md)

## What this phase builds

Phase 8 turns the Phase 7 triangle filler into a real vertex pipeline:

- **A transform path.** Object-space vertices pass through model, view, and
  reverse-Z projection matrices, a homogeneous near-plane clip, the perspective
  divide, and the viewport transform.
- **A clipper.** Triangles crossing the near plane are cut in homogeneous clip
  space before the divide and re-triangulated as fans. The side planes are
  handled by a guard band instead of six-plane clipping.
- **Back-face culling.** The signed screen-space area selects front and back
  faces under a configurable winding order.
- **A reverse-Z depth buffer.** A float target that is cleared to 0 and uses a
  greater-or-equal test, so near geometry always wins regardless of draw order.
- **Perspective-correct interpolation.** Colors are interpolated through
  `attribute / w` and `1 / w`; depth is interpolated linearly because `z_ndc`
  is already affine in screen space.
- **Cameras.** A right-handed fly camera and an orbit camera, both driven by
  plain input deltas so they are testable without a window, and both wired to
  the Phase 5 input in the sample.
- **A visible sample.** `samples/03_depth` renders a rotating, depth-tested cube
  over a perspective checkerboard floor, with orbit and fly modes, a frame-time
  overlay, `--capture`, and `--frames`.

The roadmap done-when line is: *a rotating depth-tested cube renders with no
artifacts, including when the camera moves through the near plane.* The build
guide sharpens it: *unit tests cover the clipper's output.*

## What it deliberately leaves out

- Textures, lighting, and arbitrary vertex varyings. Phase 9 adds textures and
  C++ vertex/pixel shaders; Phase 8 carries one interpolated color.
- Index buffers. The pipeline takes a triangle list; the OBJ loader in Phase 9
  introduces indices together with meshes.
- Six-plane clipping. The near plane is clipped exactly; the side planes use a
  guard band, as the build guide specifies. Geometry outside the guard is
  dropped and counted.
- Anti-aliasing, blending, and alpha composition. A visible pixel overwrites;
  an occluded pixel is rejected before it is shaded.
- Hierarchical depth, early-Z, and depth prepasses. The software renderer tests
  depth per sample.
- GPU depth formats, viewports, scissors, and swapchains. Those arrive with the
  RHI in Phases 11 to 14.
- Camera collision, animation, and controller smoothing. The cameras are math
  and input mapping only.

## Interfaces it touches

### New `cyber_software_renderer` public headers

| Header | Responsibility |
|---|---|
| `depth_buffer.h` | `DepthBufferDesc`, `DepthBuffer`: allocator-owned float depth with reverse-Z clear and `TestAndWrite`. |
| `camera.h` | `Camera`, `CameraView`, `CameraProjection`, `FlyCamera`, `FlyCameraInput`, `UpdateFlyCamera`, `OrbitCamera`, `OrbitCameraInput`, `UpdateOrbitCamera`, `OrbitCameraPosition`, `OrbitCameraView`, `OrbitCameraProjection`. |
| `pipeline.h` | `Vertex`, `Viewport`, `CullMode`, `FrontFace`, `PipelineDesc`, `PipelineStats`, `ClipVertex`, `ClipPolygon`, `ClipTriangleNearPlane`, `DrawTriangles`. |

### Changed headers

| Header | Change |
|---|---|
| `raster.h` | Adds `DepthRasterVertex`, `DepthRasterTriangle`, `RasterizeDepthTestedTriangles`, and `RasterStats::pixelsRejected`. The Phase 7 types and functions are unchanged. |
| `software.h` | Umbrella includes `camera.h`, `depth_buffer.h`, and `pipeline.h`. |

The module keeps its dependencies: `cyber_core`, `cyber_math`, `cyber_memory`,
`cyber_platform`, and `cyber_jobs`. No new module is activated. The public
interface changes are additive; every Phase 7 caller compiles unchanged.

## Design

### Coordinate spaces

```text
model space --model--> world space --view--> view space (RH, +Y up, -Z forward)
view space --projection--> clip space (4D: -w..w in x and y, 0..w in z)
clip space --near clip--> clip space
clip space --/w--> NDC (x,y in -1..1 with +Y up; z in 0..1 reverse-Z)
NDC --viewport--> screen space (top-left origin, +x right, +y down)
```

`cyber_math` already provides `Mat4::LookAt` and
`Mat4::PerspectiveReverseZ`/`PerspectiveInfiniteReverseZ`, with near mapped to
depth 1 and far to 0. Phase 8 uses them directly and does not duplicate the
math.

### Clip space and the near plane

Clip space is the D3D convention: `-w <= x <= w`, `-w <= y <= w`,
`0 <= z <= w`. For the perspective matrices in `cyber_math`, `w = -z_view`, so a
point in front of the camera has `w > 0`.

- The **near plane** is `z = w`. A vertex is inside when `z <= w`.
- The **far plane** is `z = 0`. A finite-far projection maps geometry beyond the
  far plane to `z < 0`, which the reverse-Z depth test rejects because the
  buffer is cleared to 0 and the test is greater-or-equal. Phase 8 therefore
  clips only the near plane; that is also the only plane whose violation can
  make the perspective divide explode.
- The **side planes** `x = +/-w` and `y = +/-w` are not clipped. After the
  divide, a vertex whose screen coordinate leaves `+/-16384` pixels rejects the
  whole triangle and increments `PipelineStats::trianglesOffscreen`. That guard
  is the 24.8 fixed-point limit already imposed by the Phase 7 rasterizer, and
  it keeps the clipper small. A camera pressed against a large polygon can
  still drop a triangle instead of clipping it; this is recorded as a known gap.

### Near-plane clipping

The clipper is one Sutherland-Hodgman pass against the plane `f(p) = p.z - p.w`:

1. Walk the three input vertices in order.
2. Emit an inside vertex unchanged.
3. When an edge crosses the plane, emit the intersection
   `p = lerp(a, b, f(a) / (f(a) - f(b)))`, interpolating the whole `Vec4` and
   the color.
4. The result is a triangle (3 vertices, unchanged) or a quad (4 vertices),
   which is fan-triangulated into one or two triangles.

The interpolation parameter is computed in `f32` from values that the near clip
keeps finite and small because `f` is a difference of clip coordinates that the
projection produced from finite view-space inputs. `ClipTriangleNearPlane` is
public so tests can assert the exact intersection: a vertex on the plane has
`z == w`, and the polygon keeps vertex order.

After clipping, every vertex must have `w > 0`; a triangle that still contains a
non-positive `w` is degenerate at the eye point and is skipped. Non-finite clip
coordinates skip the triangle as well.

### Viewport transform and winding

With the viewport rectangle `(x, y, width, height)`:

```text
screen.x = viewport.x + (0.5 * ndc.x + 0.5) * viewport.width
screen.y = viewport.y + (0.5 - 0.5 * ndc.y) * viewport.height
depth    = clamp(ndc.z, 0, 1)
```

The y flip is what turns a right-handed, y-up clip space into the top-left,
y-down screen space of ADR 0009. Depth stays in 0..1 because both the clip
volume and the depth range are 0..1.

The signed screen-space area is
`(b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)`. Positive area is
clockwise on screen and negative is counter-clockwise. Right-handed object
geometry winds counter-clockwise when seen from outside, and the projection
does not mirror it, so its front faces stay counter-clockwise on screen:
`FrontFace::CounterClockwise` is the default, matching D3D12's
`FrontCounterClockwise = TRUE` for a right-handed scene. `CullMode::Back`
rejects non-front-facing triangles, `CullMode::Front` rejects front-facing
ones, and `CullMode::None` keeps both. Zero area is always rejected as
degenerate.

Culling happens after clipping, so a clipped quad can contribute two triangles
with the same classification as the original geometry. It happens before the
rasterizer, so the tile bins never see a culled triangle.

### Depth buffer

`DepthBuffer` is a move-only, allocator-owned `f32` image:

- Size matches the color framebuffer for a draw; `DrawTriangles` validates it.
- `Clear()` writes 0, the reverse-Z far value.
- `TestAndWrite(x, y, depth)` compares `depth >= stored` and writes on success.
  NaN fails the comparison, so a broken interpolation cannot corrupt the buffer.
- The rasterizer keeps a local row pointer per tile row, like the color path,
  and tests depth after coverage but before interpolation and shading. A failing
  sample does not touch the color framebuffer.

The depth value written is the perspective-correct NDC depth. Because `z_ndc`
is an affine function of screen position, plain barycentric interpolation of
the three vertex depths is exact.

### Perspective-correct interpolation

With barycentric weights `b0, b1, b2` from the unbiased edge functions:

```text
inverseW = b0 * invW0 + b1 * invW1 + b2 * invW2
color    = (b0 * c0 * invW0 + b1 * c1 * invW1 + b2 * c2 * invW2) / inverseW
```

`invW` is `1 / clip.w` computed once per vertex. The per-pixel division is the
price of correctness; the reference renderer accepts it and the report records
the cost. When `inverseW` is 1 for all three vertices (the Phase 7 path), the
division cancels, and the Phase 7 functions deliberately keep their original
affine loop so their output stays byte-identical.

The rasterizer therefore has two inner loops over the same prepared triangle and
tile bins:

- the Phase 7 affine path, used by `FillTriangle` and `RasterizeTriangles`;
- the depth-tested path, used by `RasterizeDepthTestedTriangles`, which tests
  depth and interpolates perspective-correctly.

Both share snapping, the top-left rule, binning, and tile ownership, so the
coverage conventions cannot drift apart. `RasterStats` gains
`pixelsRejected`, the count of covered samples that failed the depth test; it is
0 on the Phase 7 path.

### Cameras

`Camera` is a plain value: position, yaw around +Y, pitch around local X, field
of view, and near/far planes. Yaw 0 looks toward -Z and positive yaw turns
toward +X; positive pitch looks up. `CameraView` builds an orthonormal basis and
`CameraProjection` calls the existing reverse-Z constructor.

`FlyCamera` holds a `Camera` and a movement speed. `UpdateFlyCamera` applies yaw
and pitch deltas first, clamps pitch to `+/- (pi/2 - 1e-3)` so the basis never
degenerates, then moves along the camera's forward, right, and up axes by
`speed * dt`, with an optional fast multiplier. Movement is frame-rate
independent because the caller passes `dt`.

`OrbitCamera` holds a target, distance, yaw, and pitch. Its position is the
target plus a spherical offset; `UpdateOrbitCamera` applies deltas, multiplies
the distance by `exp(-zoomDelta)` so zoom is smooth and never crosses zero,
clamps distance to `[minDistance, maxDistance]`, and clamps pitch just short of
the poles. Panning moves the target in the camera's right/up plane.

Both controllers take input structs of plain floats. The sample maps
`cyber_platform::InputState` to those structs, so the renderer never depends on
key codes and the controller tests need no window.

### Pipeline invariants

- The image depends only on the arguments, not on worker count or tile size.
- Culling, clipping, and the depth test are deterministic and order-independent
  for distinct depths; equal depths resolve by submission order through the
  greater-or-equal test plus per-tile iteration order. The tile bins preserve
  input order, so the result is still deterministic.
- `DrawTriangles` validates the framebuffer, the depth buffer dimensions, the
  viewport, and the vertex count before touching pixels.
- Temporary clip and prepared-triangle storage comes from the caller's
  allocator and is released before returning; workers never allocate.

## Alternatives considered

- **Divide by w first, then clip in NDC.** Simple, but vertices behind the
  camera produce mirrored geometry that can cover the screen. Rejected; the
  clip must precede the divide.
- **Six-plane clipping.** Removes the guard band, at the cost of a general
  clipper and more intersection cases. Deferred; the build guide specifies
  near-plane clipping plus a guard band, and the rasterizer's fixed-point limit
  already bounds screen coordinates.
- **Integer or 16-bit depth.** Faster to test, but reverse-Z's precision
  advantage comes from floating point near the near plane. Rejected.
- **Depth test with `greater`.** Would require clearing to 0 and forbidding a
  far-plane write at exactly 0. The roadmap fixes greater-or-equal and clear 0,
  which is also what D3D12's `D3D12_COMPARISON_FUNC_GREATER_EQUAL` gives.
- **A per-pixel `1/w` reciprocal approximation.** Faster, but not bit-exact
  across machines without a fixed polynomial. The reference renderer uses an
  exact division and records the cost.
- **A matrix-stack scene graph.** Phase 8 needs one model matrix per draw; a
  scene graph belongs to Phase 22.
- **Platform input in the camera API.** Would make the controllers untestable
  without a window and couple the renderer to key codes. Rejected in favour of
  plain delta structs; the sample does the mapping.

## Risks

- The side-plane guard band drops triangles that leave +/-16384 pixels. A camera
  very close to a large floor can show a missing triangle at extreme angles.
  Full six-plane clipping is the fix and is recorded as a gap.
- Per-pixel floating-point division and perspective interpolation cost more than
  the Phase 7 affine loop. The report measures the difference at 1280x720.
- Equal-depth fragments resolve by submission order. That is deterministic and
  matches the greater-or-equal rule, but coplanar geometry still z-fights, as on
  a GPU without a depth bias.
- A clipped quad's fan split introduces an edge that was not in the input
  triangle. The split edge is shared by the two output triangles with opposite
  direction, so the top-left rule still covers each pixel once.
- The camera basis uses `f32` trigonometry per frame. It is negligible at one
  camera per frame but is not a hot-path API.
- The depth buffer doubles the per-pixel memory of the RGBA32F target: 8 bytes
  per pixel for color plus 4 for depth at 1280x720 is about 11 MiB.

## Test plan

`software_depth_buffer`:

- descriptor validation, stride handling, overflow rejection, allocator balance;
- clear writes 0 and leaves padding untouched;
- greater-or-equal test writes on equal and nearer values and rejects farther
  ones;
- out-of-range access is safe;
- move semantics and invalid buffers.

`software_camera`:

- yaw 0 looks toward -Z and the basis is orthonormal;
- view matrix maps the camera to the origin and the target onto -Z;
- projection maps near to depth 1 and far to 0, and the aspect scales x;
- fly movement follows the camera axes and is independent of frame rate;
- pitch clamps short of the poles for both controllers;
- orbit position matches the spherical formula, distance clamps, and zoom never
  crosses zero.

`software_pipeline`:

- a screen-aligned quad transforms to the expected pixels with the viewport
  offset and y flip;
- near-plane clipping keeps the visible part of a triangle, produces a quad and
  two triangles, and every output vertex satisfies `z <= w` and `w > 0`;
- a triangle with a vertex behind the camera does not wrap around the screen;
- back-face culling rejects clockwise/counter-clockwise geometry according to
  the configured winding, and `CullMode::None` draws both;
- the depth test makes draw order irrelevant for distinct depths, equal depths
  resolve deterministically, and the depth buffer keeps the nearest value;
- perspective-correct color matches the analytic `attribute / w` result and
  differs from affine interpolation on a strongly foreshortened triangle;
- a clipped cube rendered with the real projection has no pixel with a stale
  depth and its serial and parallel images are byte-identical;
- invalid framebuffers, mismatched depth buffers, invalid viewports, and
  non-multiple-of-three vertex counts return `InvalidArgument`.

`software_depth_cube` (deterministic render):

- the fixed cube pose renders the front faces, the depth buffer contains only
  values the triangles wrote, and the hidden back faces never win;
- serial and job-system runs, and tile sizes 16/64/128, produce identical color
  and depth bytes;
- a camera inside the cube renders its interior surfaces through the near
  clipper without NaN pixels.

Manual evidence: `cyber_sample_depth` shows a rotating cube over a checkerboard
floor at 60 Hz, orbit and fly modes, and `--capture` writes two byte-identical
images for both the standard pose and the inside-the-cube pose.

## Sources

- Scratchapixel, "The Visibility Problem", "The Projection Stage", and the
  clipping chapter (coordinate spaces and homogeneous clipping).
- Microsoft, "Rasterization Rules" (Direct3D 11) and the Direct3D clip-space
  documentation (`-w..w`, `0..w`, viewport transform).
- Fabian Giesen, "A trip through the Graphics Pipeline 2011", parts 2, 3, 5, and
  6 (clip space, perspective correction, depth precision, guard bands).
- Steven T. Barrett, "Depth Precision Visualized" (reverse-Z and float depth).
- Tomas Akenine-Möller et al., *Real-Time Rendering*, chapters on the graphics
  pipeline, clipping, and the depth buffer (background reading).
- Sutherland and Hodgman, "Reentrant Polygon Clipping", CACM 1974 (the clipping
  algorithm).
- CyberGraphics ADR 0002 (math and depth conventions), ADR 0009 (rasterization
  conventions), ADR 0010 (depth and clipping), and the Phase 7 design note.
