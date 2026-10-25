# 0012 — Supersampling before multisample coverage

- **Status:** accepted
- **Date:** 2026-10-25
- **Deciders:** @TheCyberKiddd
- **Related:** Phase 9, [Phase 9 report](../reports/phase-09.md), ADR 0008,
  ADR 0009

## Context

The Phase 9 rasterizer takes one sample at the center of each pixel, the same
model ADR 0009 fixed when it chose a 24.8 fixed-point sample at
`(x + 0.5, y + 0.5)`. That is correct and cheap, and it makes every triangle
fully covered or absent per pixel. It also means a silhouette steps in whole
pixels, which is what the Phase 9 sample image showed: a cube against a
background with a staircase edge and a checker that crawls on the receding
faces.

Three ways to fix it exist, and they differ in cost and in how much of the
pipeline they move:

1. **Multisample coverage in the rasterizer.** Keep one shaded color per pixel
   and store `k` coverage bits plus `k` depths. Edges get correct coverage at
   roughly `1 + k/4` times the fill cost. It changes the depth buffer, the
   depth test, and every golden reference, and it is the path the GPU renderer
   will use anyway.
2. **Analytic edge coverage.** Compute the exact area of the pixel square inside
   each edge. Highest quality per sample, but it only fixes edges, not texture
   minification, and it interacts badly with overlapping triangles and with
   depth writes.
3. **Supersampling at the caller.** Render the frame into a target `n` times
   larger per axis and box-filter it down. It costs `n^2` times everything, it
   reduces edge aliasing *and* texture aliasing at once, and it needs no change
   to a single engine header.

Option 3 wins for Phase 9 because the phase's done-when line is a rendered
image, not a rasterizer feature, and because ADR 0009's single-sample contract
is what the five committed golden references pin. Changing coverage now would
have forced those references to be regenerated in the same commit that also
introduced the feature, which removes their value as a regression net.

## Decision

1. **The rasterizer keeps one center sample per pixel.** Nothing in
   `cyber_software_renderer` changes for antialiasing in Phase 9.
2. **Antialiasing is a caller decision, implemented in `samples/04_textured`.**
   The sample renders into a target `supersample` times larger per axis and
   box-filters to the output size. Averaging happens in linear `RGBA32F` before
   the sRGB transfer function, so the result is the mean radiance of the covered
   samples rather than the mean of their encoded values.
3. **The interactive path is bounded.** The window lowers the factor until the
   offscreen target fits 2560x1440 pixels, so a full-screen window degrades
   quality rather than frame rate. `--supersample 1` disables the path.
4. **Golden references stay at one sample per pixel and nearest sampling.** They
   pin rasterizer behavior; they are not beauty renders. A change to the
   sample's quality path must not move them.
5. **The material has to be resolvable.** Bilinear magnification blends over one
   texel, so the sample's checker uses 32-texel blocks in a 256-texel texture:
   at two texels per block the border would span a quarter of every check and
   read as a blur rather than an edge.

## Consequences

- The sample's quality is not available to an embedder. A caller that wants
  antialiasing today must implement the same resolve loop; Phase 14's RHI
  exposes multisample targets instead, and this ADR's sample-local code becomes
  an example of the idea rather than the mechanism.
- Supersampling costs `n^2` times the fill and `n^2` times the memory. The
  measured cost at 1280x720 in release is 23.3 FPS at a factor of 1 and 14.6 FPS
  at 2, on a CPU rasterizer with no GPU path.
- Edge quality depends on `n`. A factor of 4 gives 16 samples per output pixel
  and a clean silhouette; a factor of 2 shows a residual step on near-vertical
  edges.
- The option is clamped to 4 in both paths. A capture could afford more because
  it renders one frame, but a factor of 8 needs 5120x2880 and about 236 MB for
  the `RGBA32F` color target alone, which is not worth a fourth digit of edge
  quality. The window applies its own ceiling on top of that.
- When Phase 15 or 16 adds multisample coverage, this ADR does not block it: the
  sample can switch to a multisample target and keep the resolve loop, or drop
  it. The golden references and the rasterizer contract stay untouched either
  way.
