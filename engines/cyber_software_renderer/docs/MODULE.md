# cyber_software_renderer — module contract

**Kind:** product
**Layer:** 4
**Order:** 41
**Status:** active
**Owner:** @TheCyberKiddd

## Purpose

CPU reference renderer: framebuffer, rasterization, depth, textures, shaders,
golden images, and the second plugin. In Phase 8 it owns the linear framebuffer,
sRGB present conversion, image export, frame pacing, the bitmap-font overlay,
line drawing, the tile-parallel edge-function triangle rasterizer, the
reverse-Z depth buffer, the clipped and culled vertex pipeline, the orbit and
fly cameras, and perspective-correct interpolation. Phase 9 adds textures and
CPU shaders.

## Public API

`include/cyber/software/`

| Header | Contents |
|---|---|
| `framebuffer.h` | `PixelFormat`, `FramebufferDesc`, `Framebuffer`, `PixelSizeBytes`, `QuantizeUnorm8`, `EncodeSrgb8`. |
| `present_target.h` | `PresentByteSize`, `PackPresentPixels`. |
| `image_io.h` | `ImageFormat`, `ImageWriteDesc`, `EncodeImage`, `WriteImage`. |
| `debug_text.h` | 5x7 glyph metrics, `TextMetrics`, `MeasureText`, `DrawText`. |
| `frame_pacer.h` | `FramePacer`. |
| `raster.h` | `RasterVertex`, `RasterTriangle`, `RasterizerDesc`, `RasterStats`, `DrawLine`, `FillTriangle`, `RasterizeTriangles`, `DepthRasterVertex`, `DepthRasterTriangle`, `RasterizeDepthTestedTriangles`. |
| `depth_buffer.h` | `DepthBufferDesc`, `DepthBuffer`. |
| `camera.h` | `Camera`, `CameraForward`, `CameraRight`, `CameraUp`, `CameraView`, `CameraProjection`, `FlyCamera`, `FlyCameraInput`, `UpdateFlyCamera`, `OrbitCamera`, `OrbitCameraInput`, `UpdateOrbitCamera`, `OrbitCameraPosition`, `OrbitCameraView`, `OrbitCameraProjection`. |
| `pipeline.h` | `Vertex`, `Viewport`, `CullMode`, `FrontFace`, `PipelineDesc`, `PipelineStats`, `ClipVertex`, `ClipPolygon`, `ClipTriangleNearPlane`, `DrawTriangles`. |
| `software.h` | Umbrella include. |

Only headers under this path are public. Everything under `src/` is private.
Public headers are self-contained, never include OS headers, and never include
another module's private headers.

### Failure and ownership

- All functions are non-throwing. Recoverable failures return
  `Result<T, Error>` or `Status<Error>`; invalid arguments are reported as
  `ErrorCode::InvalidArgument`.
- `Framebuffer` is move-only and owns allocator-backed storage. `Create` is the
  only operation that allocates. Destruction releases the block through the
  allocator passed to `Create`.
- Pixel get/set outside the image is safe: reads return transparent black and
  writes are ignored. Rectangle operations clip.
- `EncodeImage` returns an allocator-backed `DynamicArray<Byte>`; `WriteImage`
  writes through `cyber_platform` and owns nothing.
- `DrawText`, `DrawLine`, `FillTriangle`, and `FramePacer` are allocation-free.
- `RasterizeTriangles` and `RasterizeDepthTestedTriangles` allocate temporary
  prepared-triangle and tile-bin arrays from the caller's allocator and release
  them before returning. Workers do not touch the allocator. Invalid targets and
  null spans return `ErrorCode::InvalidArgument`; a triangle or bin count beyond
  32 bits returns `ErrorCode::Overflow`; allocation failure returns
  `ErrorCode::Internal`.
- `DepthBuffer` is move-only and owns allocator-backed storage. `Create` is the
  only operation that allocates. Depth is a float reverse-Z value: 1 at the near
  plane, 0 at the far plane, cleared to 0, and tested greater-or-equal.
- `DrawTriangles` allocates nothing; it clips into a fixed-capacity local
  polygon and hands the result to the depth-tested rasterizer with the caller's
  allocator. It validates the color target, the depth target dimensions, the
  viewport, and the vertex count, and returns `ErrorCode::InvalidArgument` for
  a mismatch.
- `ClipTriangleNearPlane` is allocation-free and returns a value.
- The camera values and controllers are allocation-free plain data.

## Dependencies

Allowed: cyber_core, cyber_math, cyber_memory, cyber_platform, cyber_jobs

Forbidden: cyber_rhi, cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

`cyber_assets`, `cyber_scene`, and `cybergraphics_api` join this list when
Phases 9 and 25 need them. They have no targets yet and are not declared early.

## Invariants

- No RHI dependency and no graphics-backend headers; pure CPU.
- Color is stored linear. sRGB encoding happens only in `PackPresentPixels` and
  `EncodeImage`, never in framebuffer storage.
- Deterministic output for golden images: no timestamps, no random seeds, no
  locale-dependent formatting, and no address-dependent behavior.
- Rasterization snaps vertices to 24.8 fixed point, uses integer edge functions
  with the top-left rule, and bins triangles into screen tiles. Worker count and
  tile size never change the image bytes.
- Clip space is `-w..w` in x and y and `0..w` in z. Only the near plane `z = w`
  is clipped; the far plane is rejected by the reverse-Z test, and the side
  planes use a `+/-16384`-pixel guard band that drops and counts the triangle.
- Depth is perspective-correct NDC depth: interpolated barycentrically, tested
  greater-or-equal, and written only when the test passes. Colors use
  `attribute / w` and `1 / w`; the Phase 7 affine functions keep their original
  arithmetic and output.
- The viewport maps NDC to screen with a y flip; positive screen-space signed
  area is clockwise and negative is counter-clockwise.
  `FrontFace::CounterClockwise` is the default front face for right-handed
  geometry, matching D3D12's `FrontCounterClockwise = TRUE`. Culling happens
  after clipping and before binning.
- Every runtime buffer uses an explicit allocator.
- Row access is stride-aware; padding bytes are never written by pixel
  operations.
- Implements the stable API in `include/cybergraphics/` when Phase 25 lands.

## Non-goals

- Performance parity with the GPU.
- Production rendering, HDR, tone mapping, and color management beyond sRGB.
- Image loading; Phase 6 writes images only.
- vsync, swapchains, and GPU presentation (`cyber_rhi`, Phase 13).
- The stable plugin ABI; it stays reserved for Phase 25.

## Tests

`software_framebuffer`: descriptor validation, formats, stride padding, clear,
pixel access, clipping, fill, blit, conversion, moves, and allocator balance.
`software_present`: sRGB packing, alpha, explicit stride, determinism, and
validation.
`software_image_io`: exact PPM/TGA headers, pixel order, sRGB versus linear
encoding, deterministic in-memory encoding, file round-trip, and error paths.
`software_text`: metrics, glyph coverage, scaling, clipping, and blank glyphs.
`software_frame_pacer`: deadline planning, fixed-grid advance, stall resync, and
statistics.
`software_raster`: all-octant, reversed, degenerate, non-finite, and clipped
lines; the top-left fill rule on shared edges and vertices; winding and color
interpolation; invalid geometry and guard-band rejection; statistics; and
RGBA8 quantization.
`software_raster_coverage`: a 10,000-triangle jittered mesh covers 640x360
exactly once; single-threaded and job-system outputs are byte-identical across
tile sizes 16/64/128 and one/four workers.
`software_screenshot`: fixed scene encoded twice is byte-identical; PPM and TGA
files read back equal.
`software_depth_buffer`: descriptor validation, stride padding, reverse-Z
clear, greater-or-equal test/write, out-of-range safety, and moves.
`software_camera`: camera basis and view/projection mapping, fly movement,
frame-rate independence, pitch and distance clamps, and orbit zoom.
`software_pipeline`: viewport transform and y flip, near-plane clipping output,
behind-camera rejection, winding and culling, depth ordering, perspective-correct
color, clipped-cube determinism, and invalid-input handling.
`software_depth_cube`: the fixed cube renders with hidden faces rejected, and
serial, parallel, and tile-size variants produce identical color and depth
bytes.
`software_public_headers`: every public header compiles together.

The visible checks are `cyber_sample_raster` and `cyber_sample_depth`. Both
`--capture` modes write and compare two screenshots of a fixed frame. The Phase
6 `cyber_sample_gradient` remains the framebuffer/present sample.

## Budgets

- Present packing and image export are O(width * height) with one pass and no
  per-pixel virtual calls.
- Rasterization is O(pixels covered + triangle-tile references) with integer
  edge stepping in the inner loop. The Phase 7 path has no per-pixel division;
  the depth-tested path has exactly one for perspective-correct color.
- The vertex pipeline transforms one matrix-vector per vertex and clips at most
  one triangle into two before binning. Per-pixel work is coverage, one depth
  compare, and the interpolation divide.
- Steady-state frame loops do not allocate: the sample reuses one present buffer
  and resizes it only on a size change. `RasterizeTriangles` allocations are
  caller-owned scratch and bounded by triangle and tile counts.
- Framebuffer memory is `stride * height` bytes; `RGBA32F` costs 16 bytes per
  pixel.
- Golden-image suite runtime budget: small test resolutions, no large assets.

## Roadmap

Phases 6, 7, 8, 9, 25.
