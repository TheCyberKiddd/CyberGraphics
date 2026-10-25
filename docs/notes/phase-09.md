# Phase 9 reading notes — textures, shaders, and test scenes

Date: 2026-10-25

- Pineda (1988) and the Phase 7 notes cover coverage; Phase 9 keeps the same
  edge functions and only changes what is interpolated. The perspective-correct
  rule `attribute / w` divided by `1 / w` extends from one color to an arbitrary
  varying list without changing coverage.
- Fabian Giesen, "A trip through the Graphics Pipeline 2011, part 7" (texture
  sampling): a sampler folds address wrapping, filtering, and mip selection into
  one unit. Filtering happens in texel space: `u * width - 0.5`, then floor and
  fractional weights over four texels. Nearest is a single round.
- Microsoft, "Texture addressing modes" and the Direct3D sampler documentation:
  repeat is `frac`, clamp-to-edge saturates, and mirrored repeat is
  `1 - |frac|` on the folded half. The wrap is applied in normalized space
  before the texel fetch, so fractional weights stay correct at the seam.
- Mipmapping (Williams, 1983): each level is a box-filtered half-resolution
  copy. Trilinear sampling is bilinear inside two adjacent levels and a linear
  blend between them. Box filtering is not area-correct for non-power-of-two
  dimensions; the implementation averages the available 1..2 samples per axis
  and records the limitation.
- Scratchapixel, "Textures" and "Normal Mapping": sRGB-encoded texels must be
  decoded to linear before lighting, because lighting math is linear. The
  loaders therefore convert at load time, matching the project rule that storage
  is linear and only present/export encode sRGB.
- Lambert (1760) and Blinn (1977): Lambert uses `max(dot(N, L), 0)` with a
  `1 / pi` albedo normalization; Blinn-Phong replaces the reflection vector with
  the half vector `normalize(L + V)` and raises `dot(N, H)` to a specular power.
  Both are computed in linear space with a normalized surface normal.
- Sakamoto, "OBJ specification" (Wavefront): `v`, `vn`, and `vt` records are
  indexed from 1; a face record is `v`, `v/vt`, or `v/vt/vn`; faces may have
  more than three vertices and must be fan-triangulated. Parsers are treated as
  hostile: every index is range-checked, every line length is bounded, and a
  malformed record fails the load instead of reading out of bounds.
- Golden-image testing (Bryan Dudash and others on render regression suites):
  fix the scene, resolution, and camera, hash the output, and compare with a
  per-channel tolerance so driver or compiler differences do not fail a build.
  A difference image makes a failure reviewable. The project stores PPM because
  Phase 6 already writes it and it diffs trivially.
