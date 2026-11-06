# Phase 22 reading notes

- The phase-22 guide has no single authoritative source; this note records two
  independent explanations and one API reference.
- Lighthouse3D, [Testing Boxes](https://www.lighthouse3d.com/tutorials/view-frustum-culling/geometric-approach-testing-boxes/):
  reject a box only when it is outside the same frustum plane; retain
  intersections conservatively.
- LearnOpenGL, [Frustum Culling](https://learnopengl.com/Guest-Articles/2021/Scene/Frustum-Culling):
  use a cheap bounding volume and test it against the frustum's six planes.
- Microsoft, [BoundingFrustum::Intersects](https://learn.microsoft.com/en-us/windows/win32/api/directxcollision/nf-directxcollision-boundingfrustum-intersects%28constboundingbox_%29):
  confirms the bounding-frustum/AABB intersection operation.
- The implementation uses CyberGraphics' existing reverse-Z 0..1 frustum and
  positive-vertex AABB test; no third-party code or library is used.
- The flat contiguous proxy store is the baseline. A spatial hierarchy is
  deferred until measurements show the flat cull misses its recorded budget.
