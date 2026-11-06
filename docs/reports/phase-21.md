# Phase 21 report: render thread and frame pipelining

Date: 2026-11-06 · Phase 21 implementation and validation

## What was built

- Added the public `RenderThread` API with a fixed SPSC command ring, immutable
  copied frame packets, a default of two configurable CPU packet slots, and
  explicit nonblocking backpressure.
- Added ordered resize callbacks, zero-size minimized-surface support,
  single-threaded reference mode, startup and teardown callbacks, producer
  ownership checks, observable asynchronous callback errors, and profiler
  scopes on the producer and render owner.
- Added null-backend tests for render-owner RHI lifetime, producer/consumer
  overlap, packet copying and reuse, frame/resize order, command saturation,
  resize/shutdown stress while work is in flight, errors, invalid inputs, and
  single-thread parity.
- Refactored hot-pipeline jobs to prepare CPU-only reflection and vertex-layout
  data. RHI shader, layout, and pipeline objects are created and retired on
  the render owner.
- Added the Linux Clang ThreadSanitizer preset and wired `cyber_renderer`'s
  direct `cyber_platform` and `cyber_perf` dependencies into its module,
  package, and layer declarations.

## Decisions

See [ADR 0024](../adr/0024-render-thread-frame-pipeline.md), the
[design note](../design/phase-21-render-thread.md), and the
[reading notes](../notes/phase-21.md). ADR 0024 is proposed pending owner
review.

The game thread is the sole producer and lifecycle owner. Callback-managed RHI
objects belong to the render owner. Packet slots protect CPU snapshots only;
GPU resource reuse remains guarded by RHI frame fences. Queue saturation
returns `Busy` rather than stalling the producer.

## Evidence for done-when

The profiler test uses a thread-safe recording profiler to observe overlapping
producer publication and render-frame scopes on distinct thread IDs. The
ThreadSanitizer run covers the null-backend render-thread and hot-pipeline test
targets. The resize/shutdown stress test repeats twelve times, holds frame work
in flight, queues resize, and verifies that accepted work drains before
teardown.

| Exact command | Result |
|---|---|
| `.\scripts\build.ps1 -Preset debug -Test` | Passed; 101/101 CTest targets. |
| `.\scripts\build.ps1 -Preset release -Test` | Passed; 101/101 CTest targets. |
| `.\scripts\build.ps1 -Preset debug-asan -Test` | Passed; 101/101 CTest targets. |
| Linux TSan command below | Passed; 2/2 CTest targets, no ThreadSanitizer race reports. |
| `py -3 scripts\check_structure.py` | Passed; 16 modules and one contract. |
| `py -3 scripts\layer_check.py` | Passed; 16 modules. |
| `.\scripts\format.ps1 -Check` | Passed. |
| `git diff --check` | Passed. |
| Installed package consumer (commands below) | Passed; installed header compiled, consumer built and ran. |
| Source-tree consumer (commands below) | Passed; consumer built and ran. |

Linux ThreadSanitizer:

```powershell
wsl.exe --exec bash -lc "cd /mnt/c/Users/Main/Desktop/CyberGraphics && cmake --preset ci-linux-tsan && cmake --build --preset ci-linux-tsan --target cyber_test_renderer_render_thread cyber_test_renderer_hot_pipeline && ctest --test-dir build/ci-linux-tsan -R 'renderer_render_thread|renderer_hot_pipeline' --output-on-failure"
```

The TSan preset sets `CYBER_WARNINGS_AS_ERRORS=OFF` because Clang 21 diagnoses
existing warnings in unchanged code: unused RHI helpers (`Reject`,
`RunValidator`, `Fail`), tautological range comparisons in renderer descriptor
checks, and missing-field initializers in `bindless_table.cpp`. These warnings
are still emitted; the preset change is scoped to TSan. The Windows Debug,
Release, and Debug-ASan runs keep the repository's warnings-as-errors policy.

Installed package consumer (MSVC Release):

```powershell
cmake --install build\release --prefix build\phase21-package
. .\scripts\bootstrap.ps1 -Quiet
$prefix = (Resolve-Path 'build\phase21-package').Path
cmake -S tests\integration\package_consumer -B build\phase21-package-consumer-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$prefix"
cmake --build build\phase21-package-consumer-msvc
.\build\phase21-package-consumer-msvc\consumer.exe
```

Source-tree consumer (MSVC Debug):

```powershell
. .\scripts\bootstrap.ps1 -Quiet
cmake -S tests\integration\source_consumer -B build\phase21-source-consumer-msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build\phase21-source-consumer-msvc
.\build\phase21-source-consumer-msvc\source_consumer.exe
```

Both consumer configurations include `cyber/renderer/render_thread.h`; the
package verifier's required-header list includes it as well.

## Known gaps

- This phase proves ownership and CPU overlap with the null backend, not a
  native D3D12 frame workload or cross-adapter performance.
- Callers must retry `Busy`, keep callback context and profiler alive, and wait
  for GPU fences before releasing GPU-visible per-frame resources.
- Moving driver pipeline creation to the render owner can increase render
  callback duration. Background pipeline preparation remains CPU-only.
- The null-backend proof does not establish a native GPU performance budget.

## Sources used

- Christian Gyrling, “Parallelizing the Naughty Dog Engine Using Fibers,”
  Advances in Real-Time Rendering in Games, 2015. The guide's PDF endpoint
  returned HTTP 404; no inaccessible talk details are asserted.
- Microsoft, [Work submission in Direct3D 12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/command-queues-and-command-lists).
- CyberGraphics platform thread, profiler, RHI command-submission, allocator,
  and frame-ring contracts.

## What the next phase needs from this one

Phase 22 may publish renderer-owned scene updates through the producer queue.
Scene-proxy ownership and GPU frame-fence lifetimes must remain explicit; Phase
21 does not provide a scene model, parallel culling, or a native GPU renderer
workload.
