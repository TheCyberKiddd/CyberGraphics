# samples/01_gradient

Phase 6 visible proof for the framebuffer and present seam.

The sample renders a two-axis linear-space gradient with a moving checkerboard,
converts it to sRGB RGBA8, and blits it into a window through
`cyber::platform::Presenter`. A 5x7 bitmap overlay shows the smoothed frame time
and frame rate. `cyber::software::FramePacer` paces the loop at 60 Hz.

## Controls

| Key | Action |
|---|---|
| `Escape` | Close the window. |
| `F11` | Toggle fullscreen. |
| `F2` | Toggle the frame-time overlay. |

## Build and run

```powershell
.\scripts\bootstrap.ps1
cmake --preset debug
cmake --build --preset debug
.\build\debug\samples\01_gradient\cyber_sample_gradient.exe
```

The window shows a visible animated gradient. The overlay reads
`<frame ms> MS  <fps> FPS`. Expect roughly 60 FPS on a normal desktop; the
readout includes the CPU cost of packing and the OS blit.

## Deterministic capture

Capture mode renders one fixed frame offscreen, writes it twice, reads both
files back, and exits non-zero unless the bytes match. It needs no window
backend, so it also runs in CI.

```powershell
.\build\debug\samples\01_gradient\cyber_sample_gradient.exe --capture build/phase6_frame.ppm
.\\build\\debug\\samples\\01_gradient\\cyber_sample_gradient.exe --capture build/phase6_frame.tga
```

The second file is written next to the first as `<stem>.copy<extension>`.
Generated captures belong under `build/`, never in `tests/images/`.
