# Phase 8 reading notes — depth, clipping, and perspective

Date: 2026-10-24

- The vertex path is a sequence of coordinate spaces, and every convention must
  be written down once: model to world, world to view (right-handed, camera at
  the origin looking down -Z, +Y up), view to clip (projection), clip to NDC
  (perspective divide), NDC to screen (viewport with a y flip). The reverse-Z
  projection already in `cyber_math` maps near to 1 and far to 0 in a 0..1 depth
  range, which is the D3D convention.
- Clipping belongs in homogeneous clip space before the divide. A vertex behind
  the camera has w <= 0, and dividing first produces geometry that wraps around
  the screen. The near plane in this convention is `z = w` (not `z = 0`); the
  far plane is `z = 0`, and the depth test with a cleared-to-zero reverse-Z
  buffer already rejects anything beyond it, so the Phase 8 clipper handles the
  near plane and relies on a guard band for the side planes.
- Farbrausch/Giesen's "triangle rasterization" series and Microsoft's
  "Rasterization Rules" agree that coverage is decided in screen space after the
  viewport transform. Culling therefore uses the signed screen-space area: with
  a y-down screen, a counter-clockwise triangle in a y-up clip space becomes
  clockwise, so clockwise is the default front face for right-handed geometry.
- Perspective-correct interpolation comes from the fact that `1/w` is affine in
  screen space. Attributes divided by w are affine too; interpolating
  `attribute / w` and `1 / w` with the same barycentric weights and dividing at
  the end recovers the correct value. Interpolating the attribute directly is
  affine interpolation and produces the classic warped checkerboard floor.
- Reverse-Z depth: because `z_ndc` is itself affine in screen space, it can be
  interpolated linearly with the same barycentric weights; no divide is needed
  for the depth value. The test is `greater or equal`, and the buffer clears to
  0 so distant geometry loses to near geometry.
- An orbit camera is a spherical position around a target; a fly camera is a
  yaw/pitch orientation with local-axis movement. Both keep pitch away from the
  poles so the view basis never degenerates, and both must be testable without a
  window, so the controllers take plain input deltas rather than platform types.
- Sources read: Scratchapixel, "The Visibility Problem" and the projection and
  clipping chapters; Microsoft, "Rasterization Rules" and the D3D clip-space
  documentation; Fabian Giesen, "A trip through the Graphics Pipeline 2011"
  parts 2, 3, 5, and 6; Akenine-Möller et al., *Real-Time Rendering*, chapters on
  the graphics pipeline and clipping. Ideas only; no code copied.
