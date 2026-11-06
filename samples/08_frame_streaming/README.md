# 08_frame_streaming — 10,000-draw submission benchmark

This D3D12 sample records and submits 10,000 draws with unique transforms using
both aligned per-draw dynamic offsets and a finite bindless buffer table when
the selected adapter reports bindless support. It prints CPU recording,
queue-submission, and fence-completion distributions, and writes the same
measurements to `phase19-submission-report.txt` in the working directory.

Build and run from the repository root. By default, the sample uses the first
enumerated adapter (the preferred hardware adapter for D3D12):

```powershell
.\scripts\build.ps1 -Preset debug
.\build\debug\samples\08_frame_streaming\cyber_sample_frame_streaming.exe
```

Pass `--warp` to select Microsoft's WARP software adapter explicitly:

```powershell
.\build\debug\samples\08_frame_streaming\cyber_sample_frame_streaming.exe --warp
```

The report uses three warm-up frames and ten measured frames per available
binding path. The fence-completion measurement is CPU wall time and is not a GPU
timestamp; compare the CPU recording and queue-submission values separately.
The adapter name and build configuration are recorded so software-adapter
results are not mistaken for hardware results.
