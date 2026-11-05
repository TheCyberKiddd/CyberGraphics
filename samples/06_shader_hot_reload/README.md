# 06_shader_hot_reload — asynchronous D3D12 shader reload

This sample draws a triangle through the D3D12 RHI. It compiles the Cyber
shader files with `cyber_shaders`, creates reflected layouts and pipelines with
`cyber_renderer`, and watches the copied shader files beside the executable.
Compilation and pipeline creation run on the job system; a completed pipeline
is selected only at the start of a frame. A failed edit leaves the previous
pipeline active.

Build and run from the repository root:

```powershell
.\scripts\build.ps1 -Preset profile
.\build\profile\samples\06_shader_hot_reload\cyber_sample_shader_hot_reload.exe
```

Edit
`build\profile\samples\06_shader_hot_reload\shaders\triangle_pixel.cgs`
while the sample runs. Press `T` to switch the `TINT` keyword permutation;
press `Escape` to close. The sample prints shader compile events and writes
`shader-permutation-report.txt` beside the executable. The copied shader files
are the watched files; rebuilding copies the versions from this sample's
`shaders` source directory again.

The automated self-test performs a valid edit, a syntax-error edit, and a
restore. It checks that the valid pipeline becomes active within one second,
that the syntax error leaves the previous pipeline selected, then writes the
permutation report and exits with a non-zero status on failure:

```powershell
.\build\profile\samples\06_shader_hot_reload\cyber_sample_shader_hot_reload.exe --self-test
```

The self-test uses the D3D12 device and opens a window. Run it on a Windows
machine with a usable D3D12 adapter.
