# Phase 7 reading notes — triangle rasterization

Date: 2026-10-23

- Pineda (1988), "A Parallel Algorithm for Polygon Rasterization": the interior
  of a triangle is the intersection of three half-planes, and each half-plane is
  a linear edge function. Evaluating the functions incrementally turns coverage
  into integer adds and sign tests, and the algorithm parallelizes by pixel
  blocks without shared state.
- Microsoft, "Rasterization Rules" (Direct3D 11): a sample is inside when it is
  on a top edge and not a right edge, or on a left edge and not a bottom edge.
  For clockwise screen-space winding this becomes a per-edge direction test:
  horizontal edges going right and edges going up are top-left.
- Fabian Giesen, "A trip through the Graphics Pipeline 2011, part 6": vertex
  positions are snapped to a fixed-point subpixel grid so shared edges are
  bit-identical; the top-left rule reduces to subtracting one from the edge
  constant on non-top-left edges, which makes rasterization watertight.
- The edge-function bias also settles shared vertices: a vertex sample passes
  only in the triangle for which both incident edges are top-left, so a fan of
  triangles owns the vertex exactly once.
- Scratchapixel, "The Rasterization Stage": bounding-box traversal and
  barycentric interpolation; the stage is deterministic and data-parallel.
- Tile-based rasterization (coarse bins, then one job per tile) matches the
  project job system: tiles own disjoint pixels, so scheduling cannot change the
  image.
- Fixed-point layout chosen for this project: 24.8 signed pixels (8 fractional
  bits), i64 edge functions, guard band of +/-16384 pixels. Larger triangles are
  left to the Phase 8 clipper.
