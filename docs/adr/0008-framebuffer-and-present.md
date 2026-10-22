# ADR 0008: Framebuffer ownership and the present boundary

Date: 2026-10-22
Status: accepted

## Context

Phase 6 is the first phase that produces pixels and shows them in a window. Two
boundaries have to be right now because later phases build on them:

- **Color space.** Lighting, filtering, and blending are only correct in linear
  space. If the software renderer stores sRGB values, every later phase inherits
  wrong math in dark tones and every golden image has to be regenerated when the
  mistake is found.
- **Who touches the OS.** Only `cyber_rhi` may include backend graphics headers,
  and the platform layer must stay renderer-agnostic. The software renderer is a
  product; it must not include `<windows.h>`.

The framebuffer is also the reference for every later backend. Its row layout,
formats, and determinism rules define what "correct" means for GPU output.

## Decision

1. **The software framebuffer stores linear color.** `RGBA8Unorm` means 8-bit
   linear unorm RGB with linear alpha; `RGBA32F` means binary32 linear channels.
   No transfer function is applied on write.
2. **Conversion to sRGB happens only when pixels leave the renderer.**
   `PackPresentPixels` and `EncodeImage` apply `math::LinearToSrgb`; the
   framebuffer itself has no sRGB mode.
3. **The framebuffer owns allocator-backed storage and is move-only.** Width,
   height, format, and an explicit stride are part of the descriptor. A zero
   stride means tightly packed. Allocation happens only in `Create`.
4. **`cyber_platform::Presenter` blits a caller-owned byte span.** It accepts an
   RGBA8 byte span with dimensions and stride, swizzles to the DIB order, and
   calls `StretchDIBits`. It performs no color conversion and includes no
   renderer header. The pixel buffer stays owned by the producer.
5. **Only `cyber_software_renderer` depends on the framebuffer type.** The
   platform present API is useful to any producer (software renderer, test,
   future video path) and cannot create a dependency cycle.
6. **Frame pacing is explicit.** `FramePacer` plans absolute deadlines on the
   monotonic clock and clamps catch-up after a stall. Sleeping uses
   `platform::SleepUntilSeconds`, which does not raise global timer resolution.
7. **Screenshots are deterministic bytes.** PPM and TGA encoders write 8-bit
   sRGB by default, with no timestamps or locale-dependent formatting. The same
   framebuffer encodes to identical bytes.

## Consequences

- The framebuffer cannot be handed to the platform without conversion. This
  costs one pass per presented frame; the sample reuses a buffer and the cost is
  visible in the frame-time overlay. That is acceptable for a reference
  renderer and keeps the color boundary testable.
- `Presenter` is tied to the OS window lifetime. The contract states that the
  presenter must not outlive its window; a future swapchain API will make this
  explicit with handles.
- The present path is a CPU stretch, not vsync. Tearing is possible until the
  swapchain phase; the software path is a correctness reference, not the final
  product path.
- PPM and TGA are intentionally simple. Golden-image tooling in Phase 9 can diff
  them directly; a compressed format is not needed while images are small.
- `cyber_software_renderer` activates with only the modules that exist today.
  `cyber_assets`, `cyber_scene`, and `cybergraphics_api` are added to its
  contract when their phases arrive, rather than declaring targets that do not
  exist.
- The overlay font is project-owned and 5x7. It will not win a typography
  award, but it is deterministic, allocation-free, and trivial to test.
