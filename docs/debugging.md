# Debugging CyberGraphics

## Build a debug configuration

From the repository root, run:

```powershell
.\scripts\build.ps1 debug
```

In a debug build, the D3D12 backend enables the Direct3D debug layer and
GPU-based validation when the device uses a validation level other than `Off`
and the Windows Graphics Tools feature is available. These checks are slow;
use them to investigate errors, not for performance measurements. The RHI
validation layer also reports invalid state, lifetime, and binding operations
through its normal error path.

## Object names

Set the `label` field when creating a resource, command list, or other labelled
object. The D3D12 backend writes supported names to the native object, so PIX
and RenderDoc can show them in resource and command views. `Device::ObjectName`
reads the name back from the backend for diagnostics and tests; views, samplers,
bind groups, stale handles, and unnamed objects have no native name to report.

## PIX and RenderDoc captures

Build and run the D3D12 marker sample in Profile, then use PIX's GPU capture
workflow or RenderDoc's launch-and-capture workflow to record a frame:

```powershell
.\scripts\build.ps1 profile
.\build\profile\samples\05_d3d12_markers\cyber_sample_d3d12_markers.exe
```

Inspect the graphics command list for `Clear Pass` and `Clear backbuffer`, and
inspect resources by their labels. Consult the current
[PIX on Windows documentation](https://devblogs.microsoft.com/pix/documentation/)
and [RenderDoc documentation](https://renderdoc.org/docs/) for installation,
launch, and capture controls.

In Debug and Profile builds, the D3D12 backend emits native command-list events
around labelled render passes and nested debug labels. `SetDebugMarker` emits a
single-point marker through the native D3D12 support method. The isolated
`GpuMarker` helper uses the ANSI legacy event format with metadata `1` and a
byte count that includes the NUL terminator. `GpuMarkerScope` balances
begin/end events, while `GpuMarker::Set` emits single-point markers. The
compile-time `CYBER_GPU_MARKERS` switch makes these calls no-ops in Release and
Shipping.

Microsoft documents native `SetMarker` as an internal support method and
recommends WinPixEventRuntime for PIX marker instrumentation. CyberGraphics
does not link that runtime under its dependency policy. Accordingly, the marker
sample uses nested begin/end regions for capture-visible labels in PIX and
RenderDoc; the RHI point-marker API is covered by automated recording tests,
but its visibility in PIX is not claimed. The capture checks below are the
evidence for tool-visible labels, not the automated tests.

Device-removal logs include the HRESULT, adapter, per-queue submission/fence
values, and DRED breadcrumb/page-fault output when the runtime exposes it.
DRED enablement is process-global; CyberGraphics does not change those settings
from an individual device creation call.

To separate a native driver/runtime failure from RHI initialization, build and
run the standalone D3D12 probe. It creates a device, direct queue, command
allocator, and command list without using the RHI or its GPU allocator:

```powershell
. .\scripts\bootstrap.ps1 -Quiet
cmake --build --preset profile --target cyber_rhi_d3d12_probe
.\build\profile\engines\cyber_rhi\cyber_rhi_d3d12_probe.exe
```

The probe reports the HRESULT and `GetDeviceRemovedReason` after each native
operation. A failure at the same operation as the RHI sample points to the
adapter/runtime path; a successful probe means the RHI setup needs further
isolation. This diagnostic is not a workaround and does not change renderer
selection.

## Systems without a working D3D12 driver

The textured cube sample uses the CPU software renderer and does not require
D3D12. On Windows, run it at native resolution for better performance on
low-power CPUs:

```powershell
.\build\profile\samples\04_textured\cyber_sample_textured.exe --supersample 1
```

This preserves basic visual rendering when the D3D12 adapter fails, but it does
not exercise GPU markers, timestamps, or capture-tool integration.
