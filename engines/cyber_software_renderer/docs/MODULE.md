# cyber_software_renderer — module contract

**Kind:** product
**Layer:** 4
**Order:** 41
**Status:** active
**Owner:** @TheCyberKiddd

## Purpose

CPU reference renderer: framebuffer, rasterization, depth, textures, shaders,
golden images, and the second plugin. In Phase 6 it owns the linear framebuffer,
the sRGB present conversion, image export, frame pacing, and the bitmap-font
overlay. Later phases add rasterization, depth, textures, and CPU shaders.

## Public API

`include/cyber/software/`

| Header | Contents |
|---|---|
| `framebuffer.h` | `PixelFormat`, `FramebufferDesc`, `Framebuffer`, `PixelSizeBytes`, `QuantizeUnorm8`, `EncodeSrgb8`. |
| `present_target.h` | `PresentByteSize`, `PackPresentPixels`. |
| `image_io.h` | `ImageFormat`, `ImageWriteDesc`, `EncodeImage`, `WriteImage`. |
| `debug_text.h` | 5x7 glyph metrics, `TextMetrics`, `MeasureText`, `DrawText`. |
| `frame_pacer.h` | `FramePacer`. |
| `software.h` | Umbrella include. |

Only headers under this path are public. Everything under `src/` is private.
Public headers are self-contained, never include OS headers, and never include
another module's private headers.

### Failure and ownership

- All functions are non-throwing. Recoverable failures return
  `Result<T, Error>` or `Status<Error>`; invalid arguments are reported as
  `ErrorCode::InvalidArgument`.
- `Framebuffer` is move-only and owns allocator-backed storage. `Create` is the
  only operation that allocates. Destruction releases the block through the
  allocator passed to `Create`.
- Pixel get/set outside the image is safe: reads return transparent black and
  writes are ignored. Rectangle operations clip.
- `EncodeImage` returns an allocator-backed `DynamicArray<Byte>`; `WriteImage`
  writes through `cyber_platform` and owns nothing.
- `DrawText` and `FramePacer` are allocation-free.

## Dependencies

Allowed: cyber_core, cyber_math, cyber_memory, cyber_platform, cyber_jobs

Forbidden: cyber_rhi, cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

`cyber_assets`, `cyber_scene`, and `cybergraphics_api` join this list when
Phases 9 and 25 need them. They have no targets yet and are not declared early.

## Invariants

- No RHI dependency and no graphics-backend headers; pure CPU.
- Color is stored linear. sRGB encoding happens only in `PackPresentPixels` and
  `EncodeImage`, never in framebuffer storage.
- Deterministic output for golden images: no timestamps, no random seeds, no
  locale-dependent formatting, and no address-dependent behavior.
- Every runtime buffer uses an explicit allocator.
- Row access is stride-aware; padding bytes are never written by pixel
  operations.
- Implements the stable API in `include/cybergraphics/` when Phase 25 lands.

## Non-goals

- Performance parity with the GPU.
- Production rendering, HDR, tone mapping, and color management beyond sRGB.
- Image loading; Phase 6 writes images only.
- vsync, swapchains, and GPU presentation (`cyber_rhi`, Phase 13).
- The stable plugin ABI; it stays reserved for Phase 25.

## Tests

`software_framebuffer`: descriptor validation, formats, stride padding, clear,
pixel access, clipping, fill, blit, conversion, moves, and allocator balance.
`software_present`: sRGB packing, alpha, explicit stride, determinism, and
validation.
`software_image_io`: exact PPM/TGA headers, pixel order, sRGB versus linear
encoding, deterministic in-memory encoding, file round-trip, and error paths.
`software_text`: metrics, glyph coverage, scaling, clipping, and blank glyphs.
`software_frame_pacer`: deadline planning, fixed-grid advance, stall resync, and
statistics.
`software_screenshot`: fixed scene encoded twice is byte-identical; PPM and TGA
files read back equal.
`software_public_headers`: every public header compiles together.

The visible check is `cyber_sample_gradient`; its `--capture` mode writes and
compares two screenshots of a fixed frame.

## Budgets

- Present packing and image export are O(width * height) with one pass and no
  per-pixel virtual calls.
- Steady-state frame loops do not allocate: the sample reuses one present buffer
  and resizes it only on a size change.
- Framebuffer memory is `stride * height` bytes; `RGBA32F` costs 16 bytes per
  pixel.
- Golden-image suite runtime budget: small test resolutions, no large assets.

## Roadmap

Phases 6, 7, 8, 9, 25.
