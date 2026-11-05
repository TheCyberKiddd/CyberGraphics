# 04_textured — textures, CPU shaders, and lighting

Renders a rotating cube with a checkerboard texture, lit by a directional light
with Lambert diffuse and a Blinn-Phong highlight. The vertex stage is
`TransformVertexShader`; the pixel stage is `LambertBlinnPhongPixelShader`
reading a bound texture that is sampled bilinearly.

The frame is rendered `--supersample` times larger per axis and box-filtered
down, which is how the silhouette is antialiased until the rasterizer has
multisample coverage. The window defaults to 1x rendering for CPU performance;
set `--supersample 2` or `--supersample 4` to trade speed for smoother edges.
The sample lowers the requested factor as needed to keep its offscreen target
within 2560x1440.

This software-renderer sample does not require D3D12 and is the visual fallback
on systems where the GPU driver cannot create a D3D12 device or command list.
It is a separate CPU path; it does not produce GPU marker captures.

## Build and run

```
cmake --preset release
cmake --build --preset release
./build/release/samples/04_textured/cyber_sample_textured
```

Controls: drag with the left mouse button to orbit, wheel to zoom, `Escape` to
close, `F11` to toggle fullscreen.

## Capture

```
./build/release/samples/04_textured/cyber_sample_textured --capture frame.ppm --supersample 4
```

The capture renders one fixed 640x360 frame, writes it twice, and exits non-zero
unless both files are byte-identical. It prints the submitted and culled
triangle counts; with back-face culling the cube submits 6 of its 12 triangles.
`--supersample 4` is the quality used for the report image: it renders
2560x1440 and resolves to 640x360, and still writes byte-identical bytes on a
second render.
