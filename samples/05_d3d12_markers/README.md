# D3D12 GPU markers

This sample opens a window and presents a visible blue clear through the
CyberGraphics RHI. Its `Clear Pass` debug region contains a nested
`Clear backbuffer` region so both appear as events in PIX and RenderDoc without
linking a capture-tool runtime.

Build and run the Profile sample for a capture:

```powershell
.\scripts\build.ps1 profile
.\build\profile\samples\05_d3d12_markers\cyber_sample_d3d12_markers.exe
```

In Debug and Profile, launch the executable through PIX or RenderDoc and capture
a presented frame. Inspect the graphics command list for the nested `Clear Pass`
and `Clear backbuffer` regions, and confirm the blue presentation. Debug builds
enable the D3D12 validation layer to surface native API misuse; Profile builds
leave it off for focused capture runs. The automated RHI tests cover validation
and point-marker recording separately. Release and Shipping intentionally
compile markers out.
The capture applications are external development tools; neither is linked
into the sample or renderer.

If D3D12 device or command-list creation fails on a system, use the CPU-rendered
cube sample for visual progress:

```powershell
.\build\profile\samples\04_textured\cyber_sample_textured.exe --supersample 1
```

That software path runs without a D3D12-capable driver; it does not emit GPU
markers and cannot replace the PIX or RenderDoc capture for this sample.
