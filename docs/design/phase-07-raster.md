# Phase 7 — Triangle rasterization

Date: 2026-10-23
Status: approved
Related: ADR 0009, `cyber_software_renderer` contract,
[Phase 6 design note](phase-06-framebuffer.md)

## What this phase builds

Phase 7 turns the linear framebuffer into a geometry target:

- **Line drawing.** A clipped, all-octant Bresenham line with a single linear
  color, used as the rasterizer warm-up and as a debug primitive for later
  phases.
- **An edge-function triangle fill.** Integer edge functions over the triangle's
  bounding box, fixed-point subpixel vertices, the top-left fill rule, and
  barycentric per-vertex color interpolation.
- **Tile-parallel rasterization.** Triangles are binned into screen tiles, and
  one job per tile fills that tile. Tiles own disjoint pixels, so scheduling
  cannot change the result.
- **A coverage proof.** A generated mesh of 10,000 triangles sharing edges and
  vertices covers every pixel exactly once, and the single-threaded and
  multi-threaded outputs are byte-identical.
- **A visible sample.** `samples/02_raster` renders an animated triangle mosaic
  at 60 Hz with the frame-time overlay, plus `--capture` and `--frames` modes.

The roadmap done-when line is: *a test image of thousands of triangles has no
gaps or double-drawn pixels along shared edges.* The build guide sharpens it:
*10,000 random triangles sharing edges cover every pixel exactly once, and the
multithreaded renderer matches the single-threaded output bit for bit.*

## What it deliberately leaves out

- Depth buffering, back-face culling, homogeneous clipping, perspective-correct
  interpolation, and camera transforms. Those are Phase 8.
- Textures, shaders, lighting, and golden-image comparison. Those are Phase 9.
- Anti-aliasing and multisampling. Phase 7 is one sample per pixel center.
- Blending and alpha composition. A covered pixel overwrites the destination.
- Scissor rectangles and viewport transforms beyond the framebuffer bounds.
- GPU rasterization; this is the CPU reference path.

## Interfaces it touches

### `cyber_software_renderer` public headers

| Header | Responsibility |
|---|---|
| `raster.h` | `RasterVertex`, `RasterTriangle`, `RasterizerDesc`, `RasterStats`, `DrawLine`, `FillTriangle`, `RasterizeTriangles`. |
| `software.h` | Umbrella include gains `raster.h`. |

`RasterVertex` carries a continuous screen position and a linear color.
`RasterTriangle` is three such vertices. `FillTriangle` is the allocation-free
single-triangle path; `RasterizeTriangles` takes a span, an optional job system,
an allocator for tile bins, and optional statistics. `jobs::JobSystem` is
forward-declared in the public header and included only by the implementation.

The module keeps its existing dependencies: `cyber_core`, `cyber_math`,
`cyber_memory`, `cyber_platform`, and `cyber_jobs`. No new module is activated.

## Design

### Screen space and winding

Screen space is top-left origin, +x right, +y down. Vertex positions are
continuous pixel coordinates; the sample point of pixel `(x, y)` is
`(x + 0.5, y + 0.5)`.

The signed area of `(v0, v1, v2)` is
`(v1.x - v0.x) * (v2.y - v0.y) - (v1.y - v0.y) * (v2.x - v0.x)`.
A positive area is clockwise on screen. The rasterizer accepts either winding:
when the area is negative it swaps `v1` and `v2`, including their colors, so
attribute order stays attached to the geometry. A zero area is degenerate and
writes nothing. Back-face culling is not applied; Phase 8 decides which winding
is front-facing.

### Fixed-point vertices

Each vertex coordinate is snapped to a signed 24.8 fixed-point value:
`round(position * 256)`. Eight fractional bits give 1/256-pixel precision.
Snapping is done once per triangle, before coverage, so two triangles that share
an exact vertex value produce bit-identical snapped positions. That is the
watertightness precondition; callers must not recompute a shared vertex through
a different expression.

Coordinates in `[-16384, 16384]` pixels are accepted (the framebuffer limit).
Triangles with non-finite or larger coordinates are skipped and counted in
`RasterStats::trianglesSkipped`. The guard band keeps every product inside
64 bits. Phase 8's clipper removes this restriction.

### Edge functions and the fill rule

For directed edge `a -> b`, the edge function is

```
E_ab(p) = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x)
```

in fixed-point units. With the three edges `e0 = v0->v1`, `e1 = v1->v2`, and
`e2 = v2->v0`, a point strictly inside a positively wound triangle has all three
functions positive. The functions are linear, so the inner loop starts at the
clipped bounding-box origin and adds a precomputed constant per x step and per y
step. No multiplication or division happens per pixel.

The top-left rule resolves samples exactly on an edge:

- a directed edge is **top-left** when `dy == 0 && dx > 0`, or `dy < 0`
  (horizontal going right, or going up in screen space, for the normalized
  clockwise winding);
- the edge constant of every non-top-left edge is reduced by one before
  evaluation;
- a sample is covered when all three biased edge functions are `>= 0`.

The per-edge bias is the integer form of the Direct3D top-left rule. For two
triangles sharing an edge with opposite directions, the edge is top-left in
exactly one of them, so exactly one writes the shared pixels. At a shared vertex
the same test requires both incident edges to be top-left; around a fan exactly
one triangle satisfies that, so the vertex is owned once. Mesh boundaries stay
half-open on their bottom and right sides, as on a GPU.

Coverage is exact integer arithmetic. There is no tolerance and no floating
point in the inside test.

### Barycentric color

With `area = E_v0v1(v2) > 0`, the barycentric weights at a sample are computed
from the **unbiased** edge values. Coverage uses the biased values, so the -1
constant is removed again before interpolation:

```
w0 = E_v1v2 / area,  w1 = E_v2v0 / area,  w2 = E_v0v1 / area
```

and the stored color is `c0 * w0 + c1 * w1 + c2 * w2` in linear space. Removing
the bias keeps vertex colors exact and avoids the constant shift that a tiny
triangle would otherwise amplify. Phase 7 interpolates the four color channels
linearly; Phase 8 changes the weights to perspective-correct ones. The
arithmetic order is fixed so every tile and every worker computes the same bits.

### Tile binning and parallelism

1. **Prepare.** Validate each triangle, snap vertices, normalize winding,
   compute the edge constants, and compute its pixel and tile bounding box.
2. **Count.** For every prepared triangle, increment a counter for each tile its
   bounding box overlaps.
3. **Prefix sum.** Turn the counters into per-tile offsets in one triangle-index
   array.
4. **Fill.** Walk the triangles again in input order and append each index to its
   tiles, preserving input order inside a tile.
5. **Rasterize.** Run one job per tile. A job iterates its triangle list,
   intersects each triangle's bounding box with the tile, and fills pixels. Each
   tile writes only its own pixels.

The default tile size is 64 pixels and is clamped to `[8, 256]`. Tiles cover the
whole framebuffer with a partial last row and column. A triangle that is larger
than a tile still uses the same clipped bounding-box loop.

The bin arrays come from the caller's allocator and are released before the call
returns. The steady-state frame loop in the sample reuses one `RasterizeTriangles`
call per frame; allocation is bounded by the tile count plus the triangle-tile
references, not by pixels.

`jobs::JobSystem::ParallelFor(tileCount, 1, ...)` supplies the workers. When the
job system is null or `RasterizerDesc::useJobs` is false, the same tile function
runs sequentially. Because the bin lists are identical and tiles are disjoint,
both paths write identical bytes. Worker count and tile size cannot change the
image; only the order in which independent tiles finish.

### Lines

`DrawLine` clips the segment to the framebuffer rectangle with Liang-Barsky,
maps the clipped endpoints to the pixels that contain them, and runs an integer
Bresenham walk in all four quadrants and both major axes. Both endpoints are
included and every pixel is written once. A degenerate segment writes one pixel.
Lines outside the framebuffer write nothing. Endpoints beyond a +/-1e7-pixel
guard band are dropped before clipping, so no float difference or clipping ratio
can overflow; the guard is far outside any framebuffer and does not affect
normal debug lines.

### Statistics

`RasterStats` reports considered triangles, drawn triangles, skipped triangles,
tiles, and the number of pixel writes. Pixel writes are counted per tile, so the
count is deterministic and needs no atomics. The coverage test uses the count to
prove that 10,000 triangles writing a full rectangle produced exactly
`width * height` writes, with every pixel non-background.

## Alternatives considered

- **Floating-point edge functions.** Simpler to write, but rounding at shared
  edges produces gaps or double-draws, and the result can differ with compiler
  contraction. Rejected.
- **Scanline rasterization.** Good on old CPUs without SIMD, but it is awkward
  to split into independent tiles and its fill rule is harder to make
  watertight. Rejected in favour of Pineda-style edge functions.
- **4 fractional bits.** Fewer bits fit in 32-bit edges, but 1/16-pixel snapping
  is visibly coarse on small triangles and makes shared-vertex tests fragile.
  Eight bits cost 64-bit edge arithmetic and are still fast in the tile loops.
- **Per-triangle parallel jobs.** Load balance depends on triangle size and the
  same pixel could be touched by many jobs only with atomics. Tile ownership is
  simpler and deterministic. Rejected.
- **Clip triangles in Phase 7.** The homogeneous clipper belongs with the vertex
  pipeline in Phase 8. Phase 7 clips the bounding box and rejects the guard-band
  outliers.
- **A separate coverage buffer API.** Statistics plus a background-color check
  prove exclusive coverage without adding a second render target type.

## Risks

- A triangle with a very large bounding box but small coverage still costs its
  full tile list. The guard band caps the worst case; Phase 8's clipper and a
  coarse hierarchical test are the real fix.
- Binning memory grows with triangles times overlapped tiles. The default 64-pixel
  tiles keep a 640x360 test mesh small; a future scene will need a coarser bin or
  a tile-local traversal.
- The 1/256-pixel grid is visible on subpixel-thin triangles. This matches GPU
  behaviour and is documented rather than hidden.
- The top-left ownership of shared vertices favours the triangle whose two
  incident edges both point down/left. Adjacent triangles can therefore pick
  different colors at the exact vertex pixel, but coverage remains exactly once.
- The sample's animation cost grows with triangle count; it uses a modest mesh
  and leaves the 10,000-triangle run to the test.
- `RasterStats` is diagnostic, not a performance counter; it does not time
  individual tiles.

## Test plan

`software_raster`:

- horizontal, vertical, shallow, steep, diagonal, reversed, single-point, and
  clipped lines; pixels outside the segment keep their value;
- a triangle fills the expected pixel set, and a two-triangle square covers each
  pixel once;
- shared diagonal edges at several angles and both diagonal directions;
- a four-triangle fan around a pixel-center vertex owns the vertex once;
- clockwise and counter-clockwise input produce identical pixels and colors;
- degenerate, non-finite, and out-of-guard-band triangles write nothing and are
  counted as skipped;
- vertex colors, the centroid color, and the `RGBA8Unorm` quantization path;
- clipping against all four framebuffer sides;
- `RasterStats::pixels` equals the number of changed pixels;
- invalid framebuffers and empty spans are handled.

`software_raster_coverage`:

- a 10,000-triangle jittered grid at 640x360 covers every pixel exactly once;
- `stats.pixels == width * height` and every pixel differs from the clear color;
- single-threaded and job-system runs are byte-identical;
- tile sizes 16, 64, and 128 produce identical bytes;
- runs with one and four workers produce identical bytes;
- repeated runs produce identical bytes.

`software_public_headers` compiles `raster.h` with every other public header.

Manual evidence: `cyber_sample_raster` shows the animated mosaic at 60 FPS, and
its `--capture` mode writes two byte-identical PPM files.

## Sources

- Juan Pineda, "A Parallel Algorithm for Polygon Rasterization", SIGGRAPH 1988.
- Microsoft, "Rasterization Rules", Direct3D 11 programming guide (top-left,
  bottom-left, and sample coverage definitions).
- Fabian Giesen, "A trip through the Graphics Pipeline 2011, part 6: Triangle
  rasterization" (edge functions, fixed-point snapping, top-left bias,
  watertightness).
- Scratchapixel, "The Rasterization Stage" and "An Overview of the Rasterization
  Algorithm" (bounding boxes and barycentric interpolation).
- Tomas Akenine-Möller et al., *Real-Time Rendering*, triangle rasterization and
  fill-rule discussion (background reading).
- Liang-Barsky line clipping, as described in *Computer Graphics: Principles and
  Practice* (clipping the line warm-up).
- CyberGraphics ADR 0002 (screen and depth conventions), ADR 0009 (rasterization
  conventions), and the Phase 6 design note.
