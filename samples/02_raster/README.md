# samples/02_raster

Phase 7 visible proof for the software rasterizer.

The sample builds a jittered 32 x 18 mesh of 1,152 triangles. Adjacent cells
share bit-identical vertices, so the mesh is a watertight partition of the
framebuffer. Each vertex carries a linear RGBA color; the rasterizer fills the
triangles with the top-left rule and interpolates the colors barycentrically.
Triangles are binned into 64-pixel screen tiles and one job per tile fills that
tile, so the image is independent of worker count and tile size.

The linear framebuffer is packed to sRGB RGBA8 and presented through
`cyber::platform::Presenter`. `cyber::software::FramePacer` paces the loop at
60 Hz, and the 5x7 bitmap overlay shows the smoothed frame time, triangle count,
and worker count.

## Controls

| Key | Action |
|---|---|
| `Escape` | Close the window. |
| `F11` | Toggle fullscreen. |
| `F2` | Toggle the overlay. |

## Build and run

```powershell
.\scripts\bootstrap.ps1
cmake --preset debug
cmake --build --preset debug
.\build\debug\samples\02_raster\cyber_sample_raster.exe
```

The window shows an animated, smoothly shaded triangle mosaic. The overlay reads
`<frame ms>  <fps>  FPS`, then `PHASE 7  TRIANGLE RASTER` and
`<count> TRIANGLES  <workers> THREADS`. Expect roughly 60 FPS on a normal
desktop.

For a bounded smoke run:

```powershell
.\build\debug\samples\02_raster\cyber_sample_raster.exe --frames 120
```

The final line reports the average render, pack, present, pacing, and event cost
per frame, plus the last frame's triangle, pixel-write, and tile counts.

## Deterministic capture

Capture mode renders one fixed frame at 640x360 offscreen, writes it twice, reads
both files back, and exits non-zero unless the bytes match. It needs no window
backend, so it also runs in CI. Capture uses the single-threaded tile path; the
image is identical to the job-system path by construction and by the coverage
test.

```powershell
.\build\debug\samples\02_raster\cyber_sample_raster.exe --capture build/phase7_raster.ppm
.\build\debug\samples\02_raster\cyber_sample_raster.exe --capture build/phase7_raster.tga
```

The second file is written next to the first as `<stem>.copy<extension>`.
Generated captures belong under `build/`, never in `tests/images/`.
