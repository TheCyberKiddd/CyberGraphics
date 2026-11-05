# Phase 15 — GPU memory and debug tooling

**Status:** complete. The Intel HD Graphics 530 sample-path bug is fixed, the
sample submits and presents, the validation suite passes, and the capture
results are consistent with the implementation.

Phase 15 adds per-device GPU heap suballocation and transient reuse, a shared
RHI validation layer, D3D12 object-name read-back, native D3D12 pass and point
markers, timestamp queries, named leak reporting, and sticky device-loss
diagnostics.

The public recording path labels a pass through `RenderPassDesc::label`; the RHI
opens and closes that pass region around the backend render pass. Nested regions
use `CommandList::BeginDebugLabel` and `EndDebugLabel`, and point markers use
`CommandList::SetDebugMarker`. Debug and Profile builds emit native D3D12 events;
Release and Shipping compile them out. No PIX runtime or other third-party
rendering dependency is linked.

The marker sample is [`05_d3d12_markers`](../../samples/05_d3d12_markers/README.md).
It clears a swapchain image blue inside nested `Clear Pass` and `Clear
backbuffer` debug regions. A native D3D12 probe succeeded on the local Intel HD
Graphics 530; the sample-path failure was an RTV allocated from the
shader-resource heap and tagged as the wrong view kind. That bug is fixed, and
the sample submits and presents on this adapter.

The sample uses nested regions for capture acceptance because Microsoft
documents native D3D12 `SetMarker` as a support API and recommends
WinPixEventRuntime for the external marker experience. The repository
intentionally keeps that runtime out of the engine and verifies the raw
point-marker path through automated RHI tests rather than a new dependency. The
observed PIX UI follows the nested label path; it does not expose the raw native
point marker as its own separate event, and that is recorded accurately rather
than inferred from replay-source strings.

See the [Phase 15 report](../reports/phase-15.md) for APIs, validation commands,
and the capture evidence used to accept the phase.
