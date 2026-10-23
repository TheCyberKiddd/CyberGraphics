# Phase 6 — Framebuffer and present

Date: 2026-10-22
Status: approved
Related: ADR 0008, `cyber_software_renderer` contract, `cyber_platform` contract,
[Phase 5 design note](phase-05-platform-jobs.md)

## What this phase builds

Phase 6 is the first visual phase. It adds the CPU image type and the smallest
honest path from pixels to a visible window:

- **`cyber_software_renderer` becomes active.** It owns the framebuffer, the
  linear-to-sRGB present conversion, image export, the frame pacer, and a tiny
  bitmap font for the frame-time readout.
- **`cyber_platform` gains an OS present seam.** `Presenter` blits a
  caller-owned RGBA8 byte span into a window client area through a Win32 DIB
  section. The platform never includes renderer headers and never owns pixels.
- **`cyber_platform` gains a sleep primitive.** `SleepUntilSeconds` and
  `SleepSeconds` make 60 Hz pacing precise without `<chrono>` and without
  process-global timer state.
- **A visible sample.** `samples/01_gradient` animates a linear-space gradient
  with a moving checkerboard, paces to 60 Hz, and draws a live frame-time and
  frames-per-second overlay.
- **Deterministic screenshots.** PPM and TGA writers plus an in-memory encoder
  prove that the same frame produces byte-identical files.

The roadmap done-when line is: *the window shows an animated gradient at a
steady frame rate with the frame time overlay, and two screenshots of the same
frame are byte-identical.*

## What it deliberately leaves out

- Triangle rasterization, depth, clipping, textures, and shaders. Those start in
  Phase 7.
- RHI swapchains, vsync, and presentation with a GPU surface. `Presenter` is an
  OS blit for the software renderer only; Phase 13 replaces it for the GPU path.
- Color management beyond the sRGB transfer function: no ICC profiles, no HDR,
  no tone mapping.
- Image loading. PPM and TGA writing only; loaders arrive with Phase 9.
- Blending, alpha composition, and anti-aliased text. The overlay writes opaque
  glyph pixels.
- Window-relative letterboxing policy. Present stretches to the client area.
- The stable plugin ABI. `include/cybergraphics/` stays empty until Phase 25.

## Interfaces it touches

### `cyber_software_renderer` public headers

| Header | Responsibility |
|---|---|
| `framebuffer.h` | `PixelFormat`, `FramebufferDesc`, `Framebuffer`, `PixelSizeBytes`. |
| `present_target.h` | `PresentByteSize`, `PackPresentPixels` (linear framebuffer to sRGB RGBA8). |
| `image_io.h` | `ImageFormat`, `ImageWriteDesc`, `EncodeImage`, `WriteImage`. |
| `debug_text.h` | `TextMetrics`, `MeasureText`, `DrawText`, and the 5x7 glyph metrics. |
| `frame_pacer.h` | `FramePacer` with deadline planning, catch-up clamping, and statistics. |
| `software.h` | Umbrella include. |

`Framebuffer` is move-only, owns allocator-backed storage, and exposes a
non-owning `Data()` pointer plus per-row access. Public functions are
non-throwing and return `Result<T, Error>` or `Status<Error>`.

### `cyber_platform` additions

| Header | Addition |
|---|---|
| `present.h` | `PresentFormat`, `PresentSource`, `Presenter`. |
| `time.h` | `SleepUntilSeconds`, `SleepSeconds`. |
| `platform.h` | Umbrella includes `present.h`. |

The platform present API accepts bytes, dimensions, an explicit stride, and a
pixel layout. It does not know about linear color, framebuffers, or formats
beyond the memory order it must swizzle for the DIB.

## Design

### Framebuffer

`FramebufferDesc` carries width, height, `PixelFormat`, and an optional
`strideBytes`. A zero stride means tightly packed rows. Stride is explicit
because row-pitch bugs are the classic source of skewed images, and because
Phase 7's tile rasterizer and Phase 9's texture upload will both want padded
rows.

Two formats exist in this phase:

| Format | Bytes per pixel | Meaning |
|---|---:|---|
| `RGBA8Unorm` | 4 | Linear RGB in 8-bit unorm, alpha 8-bit unorm. |
| `RGBA32F` | 16 | Linear RGB in binary32, alpha binary32. |

**Linear is the storage color space.** No transfer function is applied when
writing a pixel. `EncodeSrgb8`/`QuantizeUnorm8` and `math::LinearToSrgb` are
applied only when pixels leave the renderer: present packing and image export.
This keeps lighting math correct in later phases and gives one testable
boundary.

Operations are deliberately small and non-virtual:

- `Clear`, `ClearRect` (clipped), `SetPixel`, `GetPixel` (out of
  range returns transparent black, writes are ignored).
- `Blit(source, destX, destY)` clips to the destination and converts between
  formats per pixel.
- `Convert(source, destination)` requires equal dimensions; it is the whole-
  image form of `Blit`.
- `Row(y)` returns the first byte of a row; `DataSizeBytes()` includes stride
  padding.

No function allocates except `Create`. `Framebuffer` stores its `Allocator*` so
destruction releases the block through the same allocator. The storage is
16-byte aligned for the float format.

The inner loops are plain indexed loops over bytes and floats. There is no
virtual dispatch per pixel and no per-pixel error checking beyond the clipped
rectangle setup.

### Present conversion

`PackPresentPixels` writes tightly-strided or caller-strided RGBA8 where RGB is
`LinearToSrgb` encoded and alpha is quantized linearly. It validates:

- the destination is large enough for `height` rows of the requested stride;
- the stride is at least `width * 4`;
- the source framebuffer is valid.

A per-pixel `pow` costs about 140 ms for a 1280x720 frame, so packing builds a
16 KiB stack table once per call: 16384 samples of `EncodeSrgb8` over the linear
range plus a 256-entry table for `RGBA8Unorm` sources. Linear RGB is quantized to
1/16384 before the lookup, which keeps every byte within one 8-bit code of
`EncodeSrgb8`; alpha still uses `QuantizeUnorm8`. The table is a local, so there
is no global state and no allocation, and the same framebuffer always produces
the same bytes. A unit test sweeps 4097 linear values against `EncodeSrgb8` and
asserts the one-code bound.

### Platform presenter

`Presenter::Create(Window&, Allocator&)` creates a backend object. On Windows it
stores the `HWND` from `Window::NativeHandle()`. The presenter is move-only and
must not outlive the window; the contract states this.

`Present(const PresentSource&)` validates the source, queries the client
rectangle, and returns `Ok` without drawing when the window is minimized
(zero client area). Otherwise it:

1. ensures an allocator-backed scratch buffer of `clientWidth * clientHeight * 4`
   bytes;
2. expands the source into the scratch buffer in BGRA order, nearest-neighbour
   in both axes (the DIB path is a blit, not a resampler);
3. calls `StretchDIBits` with a top-down 32-bit `BI_RGB` bitmap.

The scratch buffer is allocated once and reused; a steady-state frame does not
allocate. `Destroy` releases it. A closed window returns an error value rather
than touching a stale handle.

The null backend returns `ErrorCode::Unsupported` from `Create`, matching the
window backend. Linux CI therefore compiles and links the seam without needing a
display server.

### Frame pacing

`FramePacer` keeps an absolute deadline on the monotonic clock:

- `Reset(now)` sets the next deadline to `now + period`.
- `SecondsUntilNextFrame(now)` returns zero when overdue.
- `EndFrame(now)` advances the deadline by exactly one period. If the frame is
  more than `maxCatchUpFrames` periods late, the deadline resynchronizes to
  `now + period` instead of accumulating a backlog; a stalled debugger must not
  cause a burst of immediate frames.
- `Wait()` sleeps to within 1.5 ms of the deadline and spins the rest against the
  steady clock, then calls `EndFrame`. A pure sleep rounds up to the OS timer
  quantum (about 1 ms on Windows), which pulls a 60 Hz loop down to roughly 59
  and makes the measured frame time drift above the target.
- Statistics: instantaneous frame time, an exponential moving average used by
  the overlay, and a frame counter.

Pacing uses `platform::SleepUntilSeconds`. Windows uses a high-resolution
waitable timer created per sleep call (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`);
POSIX uses `nanosleep` with a remaining-time loop. There is no process-global
timer handle, no `<chrono>`, and no `timeBeginPeriod` side effect. The spin is
bounded by the pacer's margin, so a frame that is already late returns
immediately instead of spinning.

The sample targets 60 Hz. The actual smoothness is bounded by the display's
refresh and by the OS scheduler; vsync arrives with the swapchain phase.

### Frame-time overlay

A 5x7 bitmap font covers digits, `.`, `:`, `-`, space, and `A`–`Z`. Each glyph
is five column bytes with the low five bits holding the row; `DrawText` scales
integers by 1, 2, 3, and so on, writes opaque pixels, and clips to the
framebuffer. Unsupported characters render blank so a stray lowercase byte
cannot corrupt the image.

The sample draws `16.6 MS  60.1 FPS` at the top-left over a dark backing
rectangle, using `FramePacer::SmoothedFrameSeconds`.

### Screenshots

`EncodeImage` produces bytes in an allocator-backed `DynamicArray<Byte>`;
`WriteImage` encodes and writes through `platform::WriteFile`. PPM is P6 with a
`P6\n<width> <height>\n255\n` header. TGA is uncompressed 24-bit true color,
top-down (descriptor bit 5 set), with no footer. Both encode 8-bit sRGB by
default and can store linear-unorm bytes for debugging.

Writing the same framebuffer twice must produce identical bytes: no timestamps,
no length-dependent padding, no locale-dependent formatting.

### Integration

`cyber_software_renderer` depends on `cyber_core`, `cyber_math`,
`cyber_memory`, `cyber_platform`, and `cyber_jobs`. `cyber_assets`,
`cyber_scene`, and `cybergraphics_api` are named in the long-term contract but
have no code yet; they are added when Phases 9 and 25 need them. `cyber_jobs` is
declared now because Phase 7 parallelizes rasterization inside this module.

The module installs and exports `cyber::software_renderer`, and the package and
source consumers exercise a framebuffer plus image encoding. `samples/01_gradient`
links `cyber::software_renderer` and `cyber::platform`.

## Alternatives considered

- **Store sRGB in the RGBA8 framebuffer.** Simpler present, but every later
  lighting and filtering operation would be wrong in dark tones. Rejected.
- **Put present in the software renderer.** Would need `<windows.h>` in a
  product, breaking the rule that only the platform seam touches OS headers.
  Rejected.
- **Present a `Framebuffer` from `cyber_platform`.** Would make the platform
  depend on a renderer module and would prevent the host from presenting any
  other producer. Rejected in favour of a byte span.
- **`CreateDIBSection` + `BitBlt`.** Avoids one copy, but introduces a GDI-owned
  surface whose lifetime and DPI behavior are harder to test. `StretchDIBits`
  with an allocator-owned scratch buffer is simpler and keeps ownership
  explicit.
- **`Sleep(1)` loops.** Default Windows timer granularity is 15.6 ms, which
  cannot pace 60 Hz. A per-call high-resolution waitable timer avoids raising
  global timer resolution.
- **A vector font or a third-party font.** A bitmap font is deterministic, has
  no dependency, and is trivial to test.
- **vsync now.** Belongs to the swapchain work in Phase 13; the software path
  is an OS blit.

## Risks

- `StretchDIBits` is a CPU copy per frame. At 1280x720 it is a few milliseconds
  on the target machine; larger windows are slower and the overlay makes the
  cost visible. Acceptable for a reference renderer.
- Nearest-neighbour stretching shows aliasing when the window is resized to a
  non-native size. The sample recreates the framebuffer at client size, so the
  stretch is normally 1:1.
- Sleep precision and scheduling jitter vary with the machine and CI load. The
  pacer clamps catch-up, and pacing correctness is tested with synthetic
  deadlines; real-time smoothness is the manual visible-window check.
- A hidden or minimized window has a zero client area. Present skips it and
  returns `Ok` instead of failing the frame loop.
- DPI scaling on mixed-DPI monitors changes the client size. The sample reacts
  to resize events; a DPI change without a size change only affects the window
  rect, not the pixels.
- The float framebuffer doubles memory per pixel. Phase 6 uses small windows
  and short test images; later phases choose formats per pass.

## Test plan

`software_framebuffer`:

- format sizes and descriptor validation;
- clear, pixel get/set, clipping, and rectangle fill;
- explicit stride keeps padding untouched;
- format conversion and blit with clipping in both directions;
- out-of-range access is safe;
- move semantics and allocator balance.

`software_present`:

- present byte count, sRGB encoding of known values, alpha handling;
- explicit stride leaves padding untouched;
- deterministic packing;
- undersized destinations and invalid framebuffers are rejected.

`software_image_io`:

- exact PPM and TGA headers and pixel order;
- sRGB and linear encoding paths;
- in-memory encoding is byte-identical across calls;
- file round-trip through `platform::ReadFile`;
- unreadable paths return an error.

`software_text`:

- measured width and height, including scale;
- every supported glyph draws at least one pixel;
- clipping at negative and past-edge coordinates does not touch outside pixels;
- unsupported characters render blank.

`software_frame_pacer`:

- planned deadlines for an on-time frame;
- deadline stays on the fixed grid when frames are slightly late;
- catch-up resynchronizes after a long stall;
- frame statistics update.

`software_screenshot`:

- renders a fixed gradient with a checkerboard and text at a fixed animation
  time, encodes twice, and compares bytes for equality;
- writes PPM and TGA files and reads them back.

`platform_present`:

- default presenter is invalid and reports an error;
- hidden-window create, present, resize, present again, and present after close;
- invalid sources return `InvalidArgument`;
- on platforms without a window backend, creation returns `Unsupported`.

`platform_time` gains sleep coverage: a 5 ms deadline elapses, does not return
early, and does not hang.

`software_public_headers` and `platform_public_headers` compile every public
header together and assert move-only types.

Manual evidence: run `cyber_sample_gradient` on Windows and observe a visible
animated gradient with the overlay; run its `--capture` mode twice and compare
the two files.

## Sources

- Microsoft, "Get Started with Win32 and C++", `StretchDIBits`, `BITMAPINFO`,
  `CreateWaitableTimerEx`, and `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` reference.
- Microsoft, "Acquiring high-resolution time stamps".
- IEC 61966-2-1 sRGB transfer function (already implemented in `cyber_math`).
- Netpbm PPM (P6) format description.
- Truevision TGA file format specification, version 2.0 (uncompressed true-color
  images and the top-down origin bit).
- Glenn Fiedler, "Fix Your Timestep!" (frame timing and catch-up thinking).
- C++20 standard library: `std::span`-style views are project-owned
  (`memory::Span`); no `<chrono>` in runtime code.
- CyberGraphics ADR 0008, module contracts, and Phase 5 notes.
