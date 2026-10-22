# cyber_software_renderer

**Status:** active · **Layer:** 4 · **Kind:** product

CPU reference renderer: framebuffer, rasterization, depth, textures, shaders,
golden images, and the second plugin.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/software/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

## Phase 6: framebuffer and present

- Linear `RGBA8Unorm` and `RGBA32F` framebuffers with explicit stride,
  allocator-backed storage, clear/fill/pixel/blit/convert.
- `PackPresentPixels` converts linear pixels to sRGB RGBA8 for the platform
  presenter; `WriteImage` exports PPM or TGA deterministically.
- `FramePacer` schedules 60 Hz deadlines on the monotonic clock and reports
  frame statistics for the overlay.
- A 5x7 bitmap font draws the frame-time readout.

```cpp
cyber::software::FramebufferDesc desc{};
desc.width = 640;
desc.height = 360;
desc.format = cyber::software::PixelFormat::RGBA32F;
auto created = cyber::software::Framebuffer::Create(desc, heap);
created.Value().Clear(cyber::math::Color{0.1f, 0.2f, 0.3f});
```

## Roadmap

Phases 6, 7, 8, 9, 25.
