# ADR 0009: Rasterization conventions

Date: 2026-10-23
Status: accepted

## Context

Phase 7 introduces the first geometry path in the software renderer. Coverage
rules and vertex quantization are expensive to change later: Phase 8 adds depth
and perspective interpolation, Phase 9 adds shaders, and the GPU backends must
eventually produce the same reference images. A floating-point coverage test
would leave gaps or double-draw pixels along shared edges, and a
thread-dependent traversal order would make golden images non-deterministic.

## Decision

1. **Screen space is top-left origin, +x right, +y down.** Vertex positions are
   continuous pixel coordinates; pixel centers are at `(x + 0.5, y + 0.5)`.
2. **Vertices are snapped to a 24.8 signed fixed-point grid.** Eight fractional
   bits give 1/256-pixel positions. Shared vertices must be supplied with
   bit-identical values; with the same snapped positions, two triangles sharing
   an edge rasterize that edge watertightly.
3. **The rasterizer normalizes winding.** A triangle with negative signed area
   has its second and third vertices (and colors) swapped. Positive area is
   clockwise on screen. Back-face culling is a Phase 8 policy and is not applied
   here.
4. **Coverage uses integer edge functions and the top-left rule.** With screen
   y down and clockwise winding, a directed edge is top-left when it is
   horizontal and goes right, or when it goes up. Edge constants of non-top-left
   edges are biased by -1, and a sample is covered when all three biased edge
   functions are non-negative. This covers shared edges exactly once and assigns
   a shared vertex to the one triangle whose two incident edges are both
   top-left.
5. **Attributes are interpolated barycentrically from the unbiased edge values.**
   The -1 coverage bias is removed before interpolation so vertex colors stay
   exact. Phase 7 interpolates linear vertex color. Perspective correction is
   Phase 8.
6. **Triangles are binned into screen tiles and rasterized one job per tile.**
   Binning preserves input order within a tile; tiles own disjoint pixels. The
   image is therefore independent of worker count, tile size, and scheduling.
7. **Coordinates outside a +/-16384-pixel guard band are rejected.** The guard
   keeps 64-bit edge functions in range. Triangles that cross the viewport are
   clipped by their bounding box; a general clipper arrives in Phase 8.
8. **Line endpoints outside a +/-1e7-pixel guard band are dropped before
   clipping.** The bound is far beyond any framebuffer and keeps the clipping
   ratios and endpoint reconstruction finite in 32-bit floats.

## Consequences

- Coverage and color are deterministic byte-for-byte across single-threaded and
  multi-threaded runs, which the Phase 7 coverage test asserts.
- A shared boundary is half-open: a pixel center exactly on the bottom or right
  side of a mesh can be omitted, as on a GPU. Interior meshes have no gaps.
- Vertices must be quantized consistently by the caller for watertight seams.
  The test harness shares vertex values between adjacent triangles.
- 8 fractional bits limit positional precision to 1/256 px. This is enough for
  reference images and keeps edge products inside 64 bits at the framebuffer
  size limit.
- Geometry larger than the guard band is dropped in Phase 7 rather than clipped.
  Phase 8 owns the homogeneous clipper and will remove this limitation.
- Tile size is a scheduling parameter, not an image parameter. The default is
  64 pixels; tests pin 16, 64, and 128 to the same output.
