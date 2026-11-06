# 07_materials — reflected material instances

This Windows sample creates 100 material definitions and instances, uploads
reflected `b1, space2` parameter blocks, sorts opaque, masked, and transparent
draw keys, and renders the grid through the D3D12 RHI. Press `T` to change
material 50; the new factor is uploaded and used by the next submitted frame.
Press `Escape` to close.

Build and run from the repository root:

```powershell
.\scripts\build.ps1 -Preset debug
.\build\debug\samples\07_materials\cyber_sample_materials.exe
```

The automated self-test changes material 50 after frame 20, renders through
frame 40, checks the parameter revision, and writes `material-report.txt` beside
the executable. It requires a usable D3D12 adapter:

```powershell
.\build\debug\samples\07_materials\cyber_sample_materials.exe --self-test
```

The report contains all 100 sorted draw keys and the live material revision.
The per-instance upload strategy is intentionally conservative; Phase 19 owns
frame-ring streaming and dynamic offsets.
