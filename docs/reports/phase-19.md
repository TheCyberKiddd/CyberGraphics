# Phase 19 report: constant streaming and bindless resources

Date: 2026-11-06 · Commit: Phase 19 implementation and evidence

Machine: Windows 10 Pro 22H2, Intel Core i5-6400, Intel HD Graphics 530,
driver `31.0.101.2125`, MSVC 19.44.35229.0. The Release benchmark selected the
physical Intel adapter; a separate `--warp` invocation verified the explicit
Microsoft Basic Render Driver selection.

## What was built

- Added opt-in dynamic uniform and storage-buffer offsets, mutable bind-group
  descriptors, writable and read-only storage bindings, and separate resource
  and sampler descriptor-array limits to the RHI.
- Implemented null-backend validation and D3D12 dynamic root descriptors,
  descriptor-array updates, and capability-gated bindless support.
- Added renderer frame-slot upload streams, persistent generation-counted and
  transient descriptor allocators, and finite per-frame bindless tables with
  caller-provided fallback resources.
- Added `samples/08_frame_streaming`, which records and submits 10,000
  unique-transform draws through dynamic-offset and bindless-buffer paths.

## Decisions

See [ADR 0022](../adr/0022-frame-streaming-and-bindless-resources.md) and the
[design note](../design/phase-19-binding-streaming.md). In-flight frame-slot
reuse stays explicit and fence-bound; bindless support is capability-gated;
the conventional path remains available.

## Evidence for done-when

The Release benchmark used three warm-up frames and ten measured frames per
path. Recording includes stream writes and command recording. Queue submission
is timed separately. Completion is CPU wall time from recording start until
the frame fence is reached, not a GPU timestamp.

| Command | Result |
|---|---|
| `.\scripts\build.ps1 -Preset debug -BuildOnly`; `ctest --test-dir build\debug --output-on-failure` | Passed; 99/99 tests. |
| `.\scripts\build.ps1 -Preset release -BuildOnly`; `ctest --test-dir build\release --output-on-failure` | Passed; 99/99 tests. |
| `.\scripts\build.ps1 -Preset debug-asan -Test` | Passed; 99/99 tests. |
| `.\build\release\samples\08_frame_streaming\cyber_sample_frame_streaming.exe` (three independent runs) | Passed; both paths submitted 10,000 draws on Intel HD Graphics 530. The bindless path met the 16.67 ms budget in every run. |
| `.\build\release\samples\08_frame_streaming\cyber_sample_frame_streaming.exe --warp` | Passed; explicitly selected Microsoft Basic Render Driver and ran both paths. WARP results are not used to characterize physical-GPU performance. |
| `py -3 scripts\check_structure.py`; `py -3 scripts\layer_check.py`; `. .\scripts\bootstrap.ps1 -Quiet; .\scripts\format.ps1 -Check`; `git diff --check` | Passed. |

Installed-package consumer commands (MSVC):

```powershell
cmake --install build\release --prefix build\phase19-package
. .\scripts\bootstrap.ps1 -Quiet
$prefix = (Resolve-Path 'build\phase19-package').Path
$packageConsumer = 'build\phase19-package-consumer-msvc'
cmake -S tests\integration\package_consumer -B $packageConsumer -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$prefix"
cmake --build $packageConsumer
.\build\phase19-package-consumer-msvc\consumer.exe
```

Result: passed; the installed-package consumer configured, built, and ran.

Source-tree consumer commands (MSVC):

```powershell
. .\scripts\bootstrap.ps1 -Quiet
$sourceConsumer = 'build\phase19-source-consumer'
cmake -S tests\integration\source_consumer -B $sourceConsumer -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build $sourceConsumer
.\build\phase19-source-consumer\source_consumer.exe
```

Result: passed; the source-tree consumer configured, built, and ran.

Release measurements in microseconds. Each run uses three warm-up frames and
ten measured frames per path; p95 is calculated per ten-frame batch.

| Run | Path | Recording p95 | Queue submit p95 | Record + submit p95 | Completion p95 |
|---|---|---:|---:|---:|---:|
| 1 | Dynamic offsets | 130,373 | 205 | 130,578 | 234,228 |
| 1 | Bindless indices | 9,623 | 106 | 9,729 | 11,594 |
| 2 | Dynamic offsets | 122,158 | 841 | 122,999 | 242,431 |
| 2 | Bindless indices | 6,257 | 52 | 6,309 | 8,630 |
| 3 | Dynamic offsets | 296,989 | 68 | 297,057 | 388,002 |
| 3 | Bindless indices | 5,903 | 54 | 5,957 | 7,854 |

The frame budget is 16,667 µs. The bindless path's p95 completion stayed below
budget in all three physical-GPU batches (maximum 11,594 µs); its maximum
record-plus-submit p95 was 9,729 µs. This demonstrates that 10,000 unique
transforms fit within budget through the capability-supported bindless path.
The dynamic-offset path is much slower on this adapter and does not meet the
budget; its measurements remain visible as a comparison rather than being
discarded.

The explicit WARP run reported bindless p95 record-plus-submit of 117,321 µs
and p95 completion of 119,018 µs. The sample therefore reports adapter
selection and does not use a software-adapter result to stand in for the
physical-GPU acceptance measurement.

## Known gaps

- The dynamic-offset path is CPU-recording bound on the Intel HD Graphics 530
  and misses the frame budget. The bindless path is the measured qualifying
  path on this adapter.
- No cross-vendor hardware matrix is available. The feature-absent conventional
  fallback is covered by renderer and RHI tests; the Intel adapter supports
  bindless resources.
- The generated root-level `phase19-submission-report.txt` is transient sample
  output; this report preserves the measured results.

## Sources used

- Microsoft, [Descriptor heaps overview](https://learn.microsoft.com/en-us/windows/win32/direct3d12/descriptor-heaps-overview).
- Khronos, [Vulkan descriptor sets and indexing](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#descriptorsets).

## What the next phase needs from this one

Phase 20 can rely on the explicit frame-slot lifetime, dynamic buffer offsets,
descriptor allocators, and capability-gated bindless table contracts without
assuming that bindless indexing is supported.
