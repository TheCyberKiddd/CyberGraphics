# Phase 15 report: GPU memory and debug tooling

Date: 2026-11-02
Commits: none; all Phase 15 work remains uncommitted
Status: complete; the Intel HD Graphics 530 sample-path fix and capture evidence
are accepted.
Machine: Windows 10 Pro, Intel Core i5-6400, Intel HD Graphics 530, MSVC 19.44,
CMake 3.31.6, Ninja
Design note: [phase-15-gpu-memory-debug.md](../design/phase-15-gpu-memory-debug.md)
ADR: [0018](../adr/0018-gpu-heap-suballocation-and-rhi-validation.md)
Contract: [`cyber_rhi`](../../engines/cyber_rhi/docs/MODULE.md)

## What was built

- Per-device D3D12 heap suballocation, memory statistics, and transient
  allocation reuse.
- A shared RHI validation layer for barriers, resource lifetimes, bindings, and
  transient reuse.
- D3D12 object naming and a read-back path that verifies names through the
  driver's private data.
- Native D3D12 pass regions and point markers behind `CYBER_GPU_MARKERS`.
  Labels use the ANSI event format with metadata `1` and a byte count including
  the terminating NUL. The RAII `GpuMarkerScope` balances begin/end events;
  native calls exist only in `gpu_markers.cpp`.
- Timestamp query heaps, fence-gated read-back, and timestamp-frequency
  conversion.
- Named live-resource leak reporting and sticky D3D12 device-loss state with
  queue and DRED diagnostics.
- A D3D12 clear-pass sample for external capture tools. It has no capture-tool
  library or runtime dependency.

## API example

`RenderPassDesc::label` creates a named pass region; caller labels and
single-point markers are nested inside it:

```cpp
RenderPassDesc pass{};
pass.label = "Shadow Pass";
list.BeginRenderPass(pass);
list.BeginDebugLabel("Cascade 0");
list.SetDebugMarker("Shadow atlas ready");
list.EndDebugLabel();
list.EndRenderPass();
```

Timestamp results are available through `Device::ReadTimestamps` only after a
fence signalled after the resolve has completed.

## Evidence for done-when

| Requirement | Evidence |
|---|---|
| An invalid barrier is caught with a clear message | `rhi_validation_errors_catch_a_missing_barrier` and `rhi_validation_errors_catch_a_barrier_with_the_wrong_source_state` pass. |
| GPU pass timings can be read after completion | `rhi_timestamps_read_back_after_the_completion_fence` checks ordered timestamp values, rejects an early read, and logs elapsed milliseconds. |
| Leaks are named | `rhi_leak_report_names_live_resources` reports an intentionally leaked buffer and texture, then reports zero after cleanup. |
| Device-loss state is sticky and cleanup remains possible | `rhi_device_lost_is_sticky_and_logs_dred` forces WARP removal and checks later operations and cleanup. |
| Markers are nested through the RHI | `rhi_debug_markers_passes_carry_markers_from_the_rhi` checks pass labels, nested labels, point markers, and balanced closure. Native marker calls are isolated in `gpu_markers.cpp`. |

Full build and test commands run:

| Command | Result |
|---|---|
| `.\scripts\build.ps1 debug -Test` | Passed, 87/87 |
| `.\scripts\build.ps1 profile -Test` | Passed, 87/87 |
| `.\scripts\build.ps1 release -Test` | Passed, 87/87 |
| `.\scripts\build.ps1 debug-asan -Test` | Passed, 87/87 |
| `.\scripts\build.ps1 shipping -Test` | Passed, 87/87 |
| `py -3.14 scripts/check_structure.py` | Passed, 16 modules, 1 contract |
| `py -3.14 scripts/layer_check.py` | Passed, 16 modules |
| `.\scripts\format.ps1 -Check` | Passed |
| `.\scripts\verify_package.ps1 -Preset profile` | Passed; installed Profile package consumer built and ran |
| `git diff --check` | Passed. |

## Capture evidence and acceptance

The initial Intel HD Graphics 530 failure was isolated and fixed. A native
D3D12 probe successfully created a device, direct queue, command allocator, and
command list. The marker sample's swapchain backbuffer RTV had incorrectly used
the shader-resource descriptor heap and heap kind; routing it through the RTV
heap fixed the issue. The sample now submits and presents frames on the Intel
adapter.

Fresh captures confirm the D3D12 sample works as intended. RenderDoc's event tree
contains `Clear Pass` with `Clear backbuffer` beneath it, and the preview shows
blue output for the clear pass. PIX's capture opens and shows the `Clear Pass`
region under the graphics queue, and the frame reaches present with the expected
blue output in the screenshot. The raw native `SetMarker` call is not surfaced as
its own separate PIX UI event, which matches the documented behavior of the
underlying support API. The capture-visible evidence is the nested begin/end
label path, while the point-marker path remains verified by the RHI tests.

The CPU renderer remains usable on this host. The textured cube sample now
defaults to native-resolution rendering; its two-frame 1280x720 smoke run
completed at 35.1 FPS, and its deterministic 640x360 capture succeeded. This
CPU path demonstrates visual rendering on the machine but is not evidence for
D3D12 markers or GPU capture support.

The sample demonstrates only a blue clear pass and its capture labels. There are
no shadow, geometry, lighting, post-process, or UI renderer passes in this
phase to capture or claim. No WinPixEventRuntime, DLL, package, NuGet reference,
or external header was added.

## Sources used

- Microsoft, [ID3D12GraphicsCommandList::BeginEvent](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-beginevent).
- Microsoft, [PIX on Windows documentation](https://devblogs.microsoft.com/pix/documentation/).
- RenderDoc, [How do I capture a frame?](https://renderdoc.org/docs/how/how_capture_frame.html).
- Microsoft Direct3D 12 memory-management and DRED documentation; Khronos
  Vulkan 1.3 memory allocation specification; Frostbite GDC 2017 frame-graph
  presentation, as listed in the design note.

## What the next phase needs from this one

Phase 16 starts after the Phase 15 acceptance evidence above is recorded. This
phase already supplies the fixed D3D12 sample path, validated RHI markers, and
capture evidence needed to proceed.
