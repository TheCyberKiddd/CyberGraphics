# Sample 03 — depth, clipping, and perspective

A rotating, depth-tested cube over a checkerboard floor that recedes to the
horizon, rendered by the CPU reference renderer.

- Phase: 8.
- Proves: the model/view/projection vertex path, near-plane clipping in
  homogeneous space, back-face culling, the reverse-Z depth buffer, and
  perspective-correct interpolation.
- Expected: a solid cube with hidden faces rejected, a floor whose checker
  squares shrink correctly toward the horizon, and no artifacts when the camera
  passes through the near plane.

## Build and run

```powershell
.\scripts\build.ps1 -Preset release
.\build\release\samples\03_depth\cyber_sample_depth.exe
```

Controls: Escape closes, F11 toggles fullscreen, F2 toggles the overlay, Tab
switches between the orbit and fly cameras. Drag with the left mouse button to
rotate, use the wheel to zoom, use W/A/S/D in fly mode and Q/E for down/up.
Shift is fast.

## Capture

```powershell
.\build\release\samples\03_depth\cyber_sample_depth.exe --capture build\phase8_depth.ppm
.\build\release\samples\03_depth\cyber_sample_depth.exe --capture build\phase8_inside.ppm --inside
```

Capture renders a fixed 640x360 frame, writes it twice, and fails unless both
files are byte-identical. `--inside` places the camera at the centre of the cube
with culling disabled, so the near clipper has to cut every face.

`--frames <count>` bounds the window run and prints the average draw, pack,
present, pacing, and event costs, which is how the phase report measures the
pipeline.
