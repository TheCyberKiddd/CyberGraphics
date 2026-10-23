# Phase 6 report: framebuffer and present

Date: 2026-10-23
Commits: `8572d57..38b4b6f` (framebuffer, present seam, image export, text,
pacer, sample, package integration, and the present-budget follow-up)
Machine: Windows 10 Pro 19045, Intel Core i5-6400 (4 cores, 4 threads), 16 GB
RAM, Intel HD 530, MSVC 19.44.35207 (Visual Studio 2022 Build Tools), CMake
3.31.6, Ninja preset builds

## What was built

`cyber_software_renderer` is active. The engine can allocate a linear CPU
framebuffer, draw into it, convert it to sRGB RGBA8, blit it into a window
through a platform seam, export it as an image, and pace the loop at a fixed
rate. `cyber_platform` gained the present seam and a sleep primitive.

### Framebuffer

- `Framebuffer::Create(desc, allocator)` validates width and height in
  `[1, 16384]`, the stride, and the size, then allocates one 16-byte-aligned
  block. Everything else is non-allocating and move-only.
- Storage is linear `RGBA8Unorm` or `RGBA32F`. No transfer function is applied
  on write, so later lighting phases stay in linear space.
- `Clear`, clipped `ClearRect`, bounds-checked `SetPixel`/`GetPixel`, `Blit`
  with clipping and per-pixel format conversion, and whole-image `Convert`.
- `Row(y)` and `DataSizeBytes()` expose the strided storage without giving up
  ownership. Out-of-range reads return transparent black and writes are ignored.

### Present conversion

- `PresentByteSize` reports the tightly packed byte count with overflow checks.
- `PackPresentPixels(source, destination, stride)` writes sRGB RGBA8 rows; alpha
  is quantized linearly and bytes outside the image are untouched.
- The transfer function is a 16 KiB stack lookup table built per call (16384
  samples of `EncodeSrgb8`) plus a 256-entry table for `RGBA8Unorm` sources, so
  the hot path has no `pow`. A 4097-sample test pins each byte to within one
  code of `EncodeSrgb8`.
- The result is deterministic and allocation-free.

### Image export and text

- `EncodeImage`/`WriteImage` produce PPM (P6) and uncompressed top-down TGA
  through `platform::WriteFile`, with sRGB or linear bytes, strides, and a
  caller-supplied allocator.
- A 5x7 bitmap font draws `A`-`Z`, digits, and a few punctuation marks with
  integer scaling and clipping; `MeasureText` reports the same metrics.

### Frame pacing

- `FramePacer` keeps an absolute deadline on `platform::Clock`, clamps catch-up
  to `maxCatchUpFrames`, and exposes instantaneous and smoothed frame time.
- `Wait()` sleeps to within 1.5 ms of the deadline and spins out the remainder.
  The spin is bounded and skipped when the frame is already late.

### Platform present and sleep

- `platform::Presenter::Create(window, allocator)` returns a move-only backend
  that must not outlive its window. On Windows it keeps the `HWND`, swizzles
  RGBA8 to BGRA into allocator-backed scratch, and blits with `StretchDIBits`.
  Nearest-neighbour sampling uses fixed-point column stepping, so the inner loop
  has no integer division. Other platforms and closed windows report
  `Unsupported`.
- `SleepUntilSeconds`/`SleepSeconds` use a per-call high-resolution waitable
  timer on Windows and `nanosleep` on POSIX. No global timer handle and no
  `<chrono>`.

### Sample

`samples/01_gradient` renders a two-axis linear gradient with a moving
checkerboard, packs it to sRGB, presents it, and overlays the smoothed frame
time and rate. `F2` toggles the overlay, `F11` toggles fullscreen, `Escape`
closes. `--frames <count>` is a bounded smoke run that prints the average cost
of render, pack, present, pacing, and events. `--capture` renders one fixed frame
offscreen, writes it twice, and fails unless the two files match byte for byte.

## Performance

The first complete loop ran at 6.3 FPS at 1280x720 in `release`. The measured
budget was dominated by a per-pixel `pow` in present packing:

| Step | Before | After |
|---|---:|---:|
| Render | 8.19 ms | 2.61 ms |
| Pack to sRGB RGBA8 | 142.99 ms | 6.82 ms |
| Present | 9.24 ms | 3.24 ms |
| Pacing wait | 0.28 ms | 4.03 ms |
| Events | 0.16 ms | 0.01 ms |
| Frame | 160.86 ms | 16.67 ms |

Three changes produced that:

- Packing builds the sRGB byte table once per call instead of evaluating
  `LinearToSrgb` three times per pixel.
- The sample writes `RGBA32F` rows through `Framebuffer::Row()` and hoists the
  pulse and per-column step out of the inner loop instead of paying a
  bounds-checked `SetPixel` call per pixel.
- The Win32 presenter samples columns with fixed-point stepping instead of a
  64-bit division per pixel.

The pacer also gained its 1.5 ms spin margin: a pure sleep rounds up to the
Windows timer quantum, which held the loop at 59.4 FPS even though the work fit
the budget.

## Evidence for done-when

The roadmap done-when line is: "a window shows an animated gradient at a steady
frame rate with a frame-time overlay, and two screenshots of the same frame are
byte-identical."

### Steady frame rate

`release`, 1280x720, 300 presented frames:

```text
gradient sample closed after 300 frames, smoothed 16.67 ms (60.0 FPS)
average frame: render 2.61 ms, pack 6.82 ms, present 3.24 ms, pacing 4.03 ms, events 0.01 ms
```

The smoothed frame time sits on the 60 Hz grid (16.67 ms) for the whole run, and
the five measured segments add up to the frame. `debug` is slower because the
software loops are unoptimized; the done-when evidence is the release build.

### Byte-identical screenshots

Capture mode renders a fixed 640x360 frame with the overlay, writes it twice,
reads both files back, and compares them:

```text
capture: build\phase6_frame.ppm (691215 bytes) and build\phase6_frame.copy.ppm (691215 bytes) are byte-identical
SHA256 B0AA30B8443F4A3A0119607EF4FB605C8F2A8C8964B80B6467B41940DDBB3E08
```

`software_screenshot` performs the same check inside CTest without touching the
network or the clock.

### Local presets

| Preset | Result |
|---|---|
| `debug` | 39/39 CTest entries passed |
| `release` | 39/39 CTest entries passed |
| `shipping` | 39/39 CTest entries passed |
| `debug-asan` | 39/39 CTest entries passed |

Phase 6 suites: `software_framebuffer`, `software_present`, `software_image_io`,
`software_text`, `software_frame_pacer`, `software_screenshot`,
`software_public_headers`, `platform_present`, `platform_time`, and
`platform_public_headers`. The other 29 entries are the Phase 1-5 suites.

### Package and source consumers

```text
scripts\verify_package.ps1 -Preset release
install + package consumer configure, build, run: exit 0
```

The consumers include `cyber/software/software.h` and present a packed
framebuffer, so the installed target and headers are exercised, not just found.

### Repository checks

```text
python scripts/check_structure.py
Structure check passed: 16 modules, 1 contract(s).

python scripts/layer_check.py
Layer check passed: 16 modules.

python scripts/check_format.py
Formatting check passed: 164 files.
```

### CI

GitHub Actions run
[39914162068](https://github.com/TheCyberKiddd/CyberGraphics/actions/runs/39914162068)
passed Structure, Formatting, Windows MSVC, and Linux clang + UBSan for
`38b4b6f`, the last implementation commit. The earlier run
[39779368092](https://github.com/TheCyberKiddd/CyberGraphics/actions/runs/39779368092)
covered `8572d57`, the first Phase 6 commit.

## Decisions

ADR [0008](../adr/0008-framebuffer-and-present.md) and the
[Phase 6 design note](../design/phase-06-framebuffer.md) record the binding
choices:

- Storage is linear; sRGB exists only in `PackPresentPixels` and `EncodeImage`.
- The presenter consumes caller-owned RGBA8 bytes with an explicit stride. It
  owns only scratch memory; the window owns the surface. No renderer type leaks
  into `cyber_platform`.
- `cyber_software_renderer` depends on `cyber_core`, `cyber_math`,
  `cyber_memory`, `cyber_platform`, and `cyber_jobs`. `cyber_assets`,
  `cyber_scene`, and the C ABI stay inactive until Phases 9 and 25.
- The present path trades exactness for speed: linear RGB is quantized to
  1/16384 before the sRGB table, which keeps output within one 8-bit code and is
  deterministic for a given framebuffer.
- The pacer's last 1.5 ms is a bounded spin, because Windows sleep granularity
  otherwise keeps the loop slightly below the target rate.

## Known gaps

- The Linux window backend is the null backend, so the visible sample is
  Windows-only. CI compiles and links the public API and runs the headless
  capture path on Linux.
- Presentation is CPU-side `StretchDIBits` with nearest-neighbour scaling on
  resize. No vsync, GPU surface, or colour management beyond the sRGB transfer
  function; those arrive with `cyber_rhi`.
- The sRGB present table is within one 8-bit code of `EncodeSrgb8`, not exact at
  every input. Image export uses the exact function.
- The presenter allocates scratch on the first present and on client-area growth
  only; a resize to a larger window during a frame still allocates.
- The sample's `--frames` smoke run counts presented frames, so it is a
  throughput check, not a benchmark; scene timing depends on the desktop
  compositor and on the CPU governor.
- `FramePacer` spins up to 1.5 ms per frame. A battery-conscious host can raise
  the frame period or drive `EndFrame` itself.
- The stable C ABI remains empty until Phase 25.

## Sources used

- IEC 61966-2-1 sRGB transfer function and the W3C "A Standard Default Color
  Space for the Internet - sRGB" note.
- Microsoft, `StretchDIBits`, `BITMAPINFO`, `CreateWaitableTimerExW`,
  `SetWaitableTimer`, `QueryPerformanceCounter`, `QueryPerformanceFrequency`,
  and `Sleep` reference pages.
- POSIX `nanosleep` and `clock_gettime` reference pages.
- Netpbm PPM (P6) format specification and the Truevision TGA 2.0 specification.
- Glenn Fiedler, "Fix Your Timestep!" (fixed deadlines and catch-up clamping).
- C++20 standard library: `std::sin`, `std::pow`, `std::memcpy`, `std::construct_at`.
- CyberGraphics ADR 0008, the Phase 6 design note, and the module contracts.

## What the next phase needs from this one

Phase 7 can rely on:

- `Framebuffer` with linear `RGBA8Unorm` and `RGBA32F` storage, `Row`, and
  clipped rectangle fills.
- `PackPresentPixels` and `image_io` as the only colour-space boundaries.
- `DrawText`/`MeasureText` for overlays and debug readouts.
- `FramePacer` for the 60 Hz loop and frame statistics.
- `platform::Presenter` as the window seam, so rasterization work never includes
  a backend graphics header.
- `samples/01_gradient` as the harness for the first triangle and the hidden
  `software_screenshot` pattern for deterministic golden frames.
