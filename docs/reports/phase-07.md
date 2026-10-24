# Phase 7 report: triangle rasterization

Date: 2026-10-24
Commits: `6d2f6e3` (rasterizer, sample, tests, and docs)
Machine: Windows 10 Pro 19045, Intel Core i5-6400 (4 cores, 4 threads), 16 GB
RAM, Intel HD 530, MSVC 19.44.35229 (Visual Studio 2022 Build Tools), CMake
3.31.6, Ninja preset builds

## What was built

`cyber_software_renderer` can now rasterize geometry into the linear
framebuffer. The module exposes a clipped Bresenham line, an integer
edge-function triangle fill, and a tile-parallel batch path. A 10,000-triangle
mesh covers every pixel of a 640x360 target exactly once, and the
single-threaded and multi-threaded images are byte-identical.

### Public raster API

`include/cyber/software/raster.h` adds:

- `RasterVertex` — continuous screen position plus linear RGBA color.
- `RasterTriangle` — three vertices; winding is normalized internally.
- `RasterizerDesc` — tile size in `[8, 256]` and a `useJobs` switch.
- `RasterStats` — triangles, drawn/skipped triangles, tiles, and pixel writes.
- `DrawLine`, `FillTriangle`, and `RasterizeTriangles`.

`software.h` includes the new header. `jobs::JobSystem` is forward-declared in
the public header, so callers that only need `FillTriangle` do not include the
jobs module.

### Fixed-point coverage

- Vertex positions are snapped once to signed 24.8 fixed point (1/256 pixel).
  Shared vertices supplied with the same float value produce the same snapped
  point, which is the watertightness precondition.
- Coverage uses 64-bit edge functions `E(p) = a*x + b*y + c`, evaluated
  incrementally across the clipped bounding box. The inside test is exact
  integer arithmetic with no tolerance.
- Winding is normalized to positive area (clockwise in screen space); negative
  input swaps `v1`/`v2` and their colors.
- The top-left rule for clockwise winding treats a directed edge as top-left
  when it is horizontal and goes right, or when it goes up. Non-top-left edge
  constants are biased by `-1`; a sample is covered when all biased values are
  non-negative. Shared edges and vertices are owned exactly once.
- Colors are interpolated barycentrically from the **unbiased** edge values, so
  the coverage bias does not shift vertex colors.
- Coordinates beyond a +/-16384-pixel guard band are skipped and counted.
  Non-finite and degenerate triangles are skipped.

### Tile binning and parallelism

1. Validate, snap, normalize, and compute each triangle's pixel and tile box.
2. Count triangle references per screen tile.
3. Prefix-sum the counters into per-tile offsets.
4. Append triangle indices in input order, so a tile's list is deterministic.
5. Run one job per tile; each tile writes only its own pixels.

`jobs::JobSystem::ParallelFor(tileCount, 1, ...)` supplies the workers. A null
job system or `useJobs == false` runs the identical tile function sequentially.
The image depends only on the input triangles: worker count and tile size change
scheduling, never bytes.

### Lines and statistics

`DrawLine` clips with Liang-Barsky, maps endpoints to their containing pixels,
and runs an all-octant integer Bresenham walk. Both endpoints are included;
degenerate segments write one pixel. Non-finite coordinates, coordinates beyond
a +/-1e7-pixel guard band, and segments outside the framebuffer write nothing.

`RasterStats` accumulates pixel writes per tile on the worker and sums them on
the calling thread, so it needs no atomics. A watertight full-screen mesh
reports exactly `width * height` writes.

### Sample

`samples/02_raster` builds a jittered 32 x 18 grid of 1,152 triangles with
shared vertices and linear per-vertex colors, rasterizes it through the job
system, packs the linear result to sRGB, and presents it at 60 Hz. The overlay
shows the smoothed frame time, frame rate, triangle count, and worker count.
`--frames <count>` is a bounded smoke run; `--capture` renders one fixed frame
offscreen, writes it twice, reads both files back, and fails unless they match.

## Performance

`release`, 1280x720, 300 presented frames:

```text
raster sample closed after 300 frames, smoothed 16.67 ms (60.0 FPS)
last frame: 1152 triangles, 921600 pixels, 240 tiles
average frame: render 3.10 ms, pack 7.57 ms, present 2.24 ms, pacing 3.77 ms, events 0.15 ms
```

The raster loop itself is 3.10 ms for 1,152 triangles and 921,600 pixel writes
including the overlay. Present packing remains the largest segment because every
pixel is converted to sRGB and copied; that cost belongs to the Phase 6 present
path, not to rasterization. The `debug` build is unoptimized (about 10.9 FPS,
with packing at 78 ms) and is not the performance target.

## Evidence for done-when

The roadmap done-when line is: "a test image of thousands of triangles has no
gaps or double-drawn pixels along shared edges." The build guide sharpens it:
"10,000 random triangles sharing edges cover every pixel exactly once, and the
multithreaded renderer matches the single-threaded output bit for bit."

### 10,000-triangle coverage

`software_raster_coverage` builds a 100 x 50 grid of jittered quads split into
10,000 clockwise triangles at 640x360. Shared grid vertices are generated once,
and the jitter is bounded to keep every quad convex, so the mesh is a true
partition. The test asserts:

- `triangles == 10,000`, `trianglesDrawn == 10,000`, `trianglesSkipped == 0`.
- `tiles == 60` at the default 64-pixel tile size.
- `pixels == 230,400`, exactly `640 * 360`.
- Every pixel has alpha 255 after clearing to transparent black.
- Single-threaded and 4-worker outputs are `memcmp`-equal.
- Tile sizes 16, 64, and 128 (`920`, `60`, and `15` tiles) are byte-identical.
- One-worker and repeated single-threaded runs are byte-identical.

The release suite runs this in 0.10 s, debug in 0.26 s, and ASan in 0.52 s.

### Visible sample and captures

`samples/02_raster` in `release` holds the 60 Hz grid for 300 frames and reports
the last frame above. Its fixed 640x360 capture, with the overlay, writes and
compares two files:

```text
capture: build\phase7_raster.ppm (691215 bytes) and build\phase7_raster.copy.ppm (691215 bytes) are byte-identical
SHA256 B8A16B9AC5AFF7E709D34D4430C66838FE72B5EE75C09756677E8E4BD39B8B82

capture: build\phase7_raster.tga (691218 bytes) and build\phase7_raster.copy.tga (691218 bytes) are byte-identical
SHA256 3B59500B5C087ED778B7DEDAEB0F4AA0F07D742C196077756C9C1E0F8951284D
```

The PPM is 178,507 unique colors, the overlay background is present in the top
left, and the mesh covers the full frame. Capture uses the serial tile path so
it does not depend on worker availability; the coverage test proves the serial
and job paths agree byte for byte.

### Local presets

| Preset | Result |
|---|---|
| `debug` | 41/41 CTest entries passed |
| `release` | 41/41 CTest entries passed |
| `shipping` | 41/41 CTest entries passed |
| `debug-asan` | 41/41 CTest entries passed |

Phase 7 suites: `software_raster` and `software_raster_coverage`. The other 39
entries are the Phase 1-6 suites, including `software_public_headers`, which now
compiles `raster.h` with every other public header.

### Package and source consumers

```text
scripts\verify_package.ps1 -Preset debug
scripts\verify_package.ps1 -Preset release
install + package consumer configure, build, run: exit 0
```

The package consumer includes `cyber/software/raster.h`, rasterizes one
triangle into a 2x2 framebuffer, and requires exactly four pixel writes. The
install check also requires the new header in the exported package.

### Repository checks

```text
python scripts/check_structure.py
Structure check passed: 16 modules, 1 contract(s).

python scripts/layer_check.py
Layer check passed: 16 modules.

python scripts/check_format.py
Formatting check passed: 169 files.
```

### CI

GitHub Actions run
[40072184312](https://github.com/TheCyberKiddd/CyberGraphics/actions/runs/40072184312)
passed Structure, Formatting, Windows MSVC, and Linux clang + UBSan for
`6d2f6e3`, the Phase 7 commit. Both platform jobs built the project, ran the
41-test suite, installed the package, verified the installed headers and
targets, and ran the package and source consumers.

## Decisions

ADR [0009](../adr/0009-rasterization-conventions.md) and the
[Phase 7 design note](../design/phase-07-raster.md) record the binding choices:

- Screen space is top-left origin, +y down; pixel centers are at
  `(x + 0.5, y + 0.5)`; positions are 24.8 fixed point with a +/-16384 guard.
- Coverage is integer edge functions with the clockwise top-left rule and a
  `-1` bias on non-top-left edges. Winding is normalized; back-face culling is
  a Phase 8 policy.
- Color uses unbiased barycentric weights. Phase 8 replaces the weights with
  perspective-correct ones.
- Triangles are binned into screen tiles and one job per tile owns disjoint
  pixels. Tile size is scheduling, not image state.
- Lines are clipped with Liang-Barsky and walked with integer Bresenham. Line
  endpoints outside +/-1e7 are rejected before clipping.
- `RasterStats::pixels` counts writes, not unique pixels. On a watertight
  partition it equals the framebuffer pixel count, which is the coverage proof.
- No separate coverage-buffer API was added; statistics plus the alpha check
  prove exclusivity with no extra render target.

## Known gaps

- No depth buffer, back-face culling, homogeneous clipping, perspective-correct
  interpolation, or camera transform. These are Phase 8.
- Triangles with vertices outside +/-16384 pixels are dropped rather than
  clipped. Phase 8's clipper removes this limitation.
- Positions are quantized to 1/256 pixel. Subpixel-thin triangles can show that
  grid, matching GPU behaviour but visible in a magnified reference image.
- No anti-aliasing, multisampling, blending, or alpha composition; a covered
  pixel overwrites its destination.
- No scissor rectangle or viewport transform beyond framebuffer clipping.
- Binning memory grows with triangles times overlapped tiles. A future scene
  needs a coarser hierarchy or a tile-local traversal; the guard band caps the
  Phase 7 worst case.
- `RasterizeTriangles` allocates temporary arrays on every call. The sample pays
  that cost per frame; a steady engine path will need reusable scratch.
- The raster loops are scalar and unoptimized beyond incremental edge stepping.
  SIMD and tile-specialized kernels are performance work, not Phase 7 scope.
- The visible sample is Windows-only because the Linux window backend is null.
  The headless capture path still compiles and runs on Linux CI.
- The stable C ABI remains empty until Phase 25.

## Sources used

- Juan Pineda, "A Parallel Algorithm for Polygon Rasterization", SIGGRAPH 1988.
- Microsoft, "Rasterization Rules", Direct3D 11 programming guide.
- Fabian Giesen, "A trip through the Graphics Pipeline 2011, part 6: Triangle
  rasterization".
- Scratchapixel, "The Rasterization Stage" and "An Overview of the
  Rasterization Algorithm".
- Liang-Barsky line clipping, as described in *Computer Graphics: Principles and
  Practice*.
- Tomas Akenine-Möller et al., *Real-Time Rendering* (background reading).
- C++20 standard library: `std::isfinite`, `std::lround`, `std::memcpy`.
- CyberGraphics ADR 0002, ADR 0009, the Phase 7 design note, and the module
  contracts.

## What the next phase needs from this one

Phase 8 can rely on:

- `RasterTriangle`/`RasterVertex` and `RasterizeTriangles` as the geometry entry
  point, with `RasterStats` for coverage diagnostics.
- The 24.8 snapping and clockwise top-left ownership already in place, so depth
  and perspective tests can reuse the same coverage convention.
- The tile binning path and `JobSystem::ParallelFor` for parallel fills.
- `Framebuffer` linear `RGBA32F` storage as the destination for a depth-tested
  pass, and `DrawLine` for debug overlays.
- The 10,000-triangle coverage suite as the regression gate for any clipper or
  interpolation change.
