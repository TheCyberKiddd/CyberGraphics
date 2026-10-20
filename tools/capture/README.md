# tools/capture

Frame capture inspector and replayer. Introduced in Phase 39.

Planned responsibilities:

- open a versioned capture file produced by `cyber_capture`;
- list draw calls, passes, resources, and pipeline state;
- view textures, buffers, and meshes;
- replay a capture on a chosen backend and compare the output image;
- keep PIX and RenderDoc markers working alongside the native capture.

It links `cyber::capture` and the public RHI API.
