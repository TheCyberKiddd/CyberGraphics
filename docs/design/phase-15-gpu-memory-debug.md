# Phase 15 — GPU memory and debug tooling

Date: 2026-10-27
Status: drafts landed for increments 1–2 (allocator: 813bb45; reclamation and
quarantine: 39d3395); remaining increments are in progress
Related: ADR 0018 (GPU memory allocator and the validation layer), ADR 0006
(memory allocation and handles), ADR 0013 (RHI object model), ADR 0014 (resource
model), ADR 0016 (command recording), ADR 0017 (D3D12 backend),
[`cyber_rhi` contract](../../engines/cyber_rhi/docs/MODULE.md),
[Phase 14 report](../reports/phase-14.md)

## What this phase builds

Phase 14 ended with a working backend that gives every resource its own heap and
lets the debug layer be the only thing checking the caller. Phase 15 replaces
both halves of that sentence: a GPU memory allocator that sub-allocates inside
large heaps and can reuse memory for resources whose lifetimes do not overlap,
and a validation layer that catches the API misuse the debug layer cannot,
because it survives into a release build and because it names the resource.

The done-when line is three things: a deliberately wrong barrier is caught with a
clear message, GPU pass timings are visible, and the leak report names a
deliberately leaked resource.

## What it deliberately leaves out

- **A general-purpose block allocator with defragmentation.** Heaps are never
  moved: a fragment that is too small for a request is wasted until every
  allocation in its block is released. Moving memory needs resource relocations
  and a frame boundary to do them at, which the render graph in Phase 20 owns.
- **Residency management.** Every heap stays resident. `MakeResident`,
  eviction, and an LRU budget are for the virtual-texture work in Phase 27.
- **Vulkan-style memory types.** The RHI names the three memory classes the
  engine actually uses — device-local, upload, and readback — and the D3D12
  backend maps them to `DEFAULT`, `UPLOAD`, and `READBACK` heaps. A second
  backend's richer taxonomy is a Phase 34 problem.
- **A visual profiler.** Timestamp queries produce numbers; a frame graph, a
  capture format, and the capture inspector are Phase 25 and Phase 39.
- **Automatic barrier insertion.** The validation layer reports what the caller
  got wrong. Working out what the caller *meant* is Phase 20's job, where the
  whole frame is known before anything is recorded.

## Interfaces it touches

Public (`include/cyber/rhi/`), each one argued in ADR 0018:

| Change | Why |
|---|---|
| `ErrorCode::DeviceLost` | A removed device is a recoverable, reportable state, not an `Internal` failure, and the caller has to be able to tell it apart. |
| `MemoryLifetime { Retained, Transient }` on `BufferDesc` and `TextureDesc` | The allocator may only reuse memory the caller has declared it will not need forever. Defaulting to `Retained` keeps every existing description valid and keeps today's behaviour. |
| `MemoryStats` and `Device::MemoryStats()` | The fragmentation report the build guide asks for. A caller reads it; nothing else depends on it. |
| `QueryHeapDesc`, `Device::CreateQueryHeap`/`DestroyQueryHeap`, `CommandList::WriteTimestamp`, `Device::ResolveTimestamps`, `Device::TimestampFrequency` | Pass timings, in the shape the frame ring already uses: write this frame, read the previous frame's results. |
| `Device::ReportLeaks()` | The leak report, callable at shutdown and from a test. |

`ValidationLevel` already exists in `DeviceDesc` with the phase's meaning
(`Standard` is validity and limits, `Exhaustive` adds state tracking, leak
reports, and per-call logging), so the validation layer needs no new switch: it
is what `Off` and `Standard` now mean at the RHI level rather than at the debug
layer's level.

Nothing in this phase changes an existing call's meaning. Every addition is
additive, which is what the Phase 10 object model was drawn for.

## Design

### Increments

| Increment | Contents | Evidence |
|---|---|---|
| 1. Allocator | Heaps per memory class, sub-allocation over 64 KiB segments, `CreatePlacedResource`, statistics and fragmentation | `rhi_gpu_memory` |
| 2. Aliasing | `MemoryLifetime`, reuse of a released transient's bytes, and a validation check that catches reuse while the resource is still in flight | `rhi_gpu_memory`, aliasing cases |
| 3. Validation layer | One rule set shared with the null backend, wrapping a real backend under `ValidationLevel` | `rhi_validation_errors` |
| 4. Names and markers | Every object named, pass markers emitted by the RHI, and `docs/debugging.md` | `rhi_debug_markers` |
| 5. Timestamps | Query heaps, a ring of them, per-pass timings, and the frequency | `rhi_timestamps` |
| 6. Leak report | Live resources listed by kind, label, and size, with the unlabelled ones counted | `rhi_leak_report` |
| 7. Device-lost | DRED breadcrumbs, the removal path, and a test that forces a removal | `rhi_device_lost` |

### The allocator

A block per memory class is carved from one `ID3D12Heap`. Blocks grow by
doubling from 8 MiB and stop at 256 MiB; a request larger than the largest block
size gets a dedicated heap, exactly as every resource does today, so the
allocator is a fast path rather than a new failure mode.

Inside a block, allocations are rounded up to the API's placement alignment
(64 KiB for buffers and ordinary textures, 4 MiB for multisampled ones) and
carved from a free list sorted by offset. A free list rather than a bump
allocator because resources are destroyed out of order; first fit rather than
best fit because the fragmentation report exists to tell a caller when first fit
stops being good enough.

Allocation is per device, behind the device's mutex, and returns a placement
(heap, offset, size). Nothing is freed to the OS until the device is destroyed,
which is the policy that makes `MemoryStats` meaningful: it reports blocks,
bytes reserved, bytes used, the waste inside each block, and the largest
allocation a block could still serve.

### Aliasing

`MemoryLifetime::Transient` is a promise from the caller: this resource's bytes
do not have to outlive the submission that last reads it. A transient resource
is placed where a released transient's memory was, and the allocation records
which resources have owned it.

The promise is checkable, and the checker is the point: when the allocator hands
out memory that a resource used, the validation layer compares the fence value
of the last submission that used the old resource against the new resource's
first use. Reuse before that fence is reached is a reported error naming both
resources, not a race that shows up as a wrong pixel on one machine. That check
is why aliasing is Phase 15's and not the frame graph's: the graph decides
*when* to reuse, and this layer decides whether the reuse was legal.

### The validation layer

Today the null backend carries the checks — argument validation, the recorded
state machine, per-subresource state, binding completeness — and a real backend
carries almost none of them, because the driver's debug layer is slower, is
debug-only, and does not know what the *RHI* promised.

Phase 15 extracts those checks from `cyber_rhi/src/null_backend.cpp` into
`cyber_rhi/src/validation.cpp` and gives them two callers:

- the null backend, which performs the work rather than validating it, and
- a wrapping `DeviceInterface`/`CommandListInterface` that sits in front of a
  real backend and forwards every call after checking it.

One implementation, so a rule cannot be enforced in one place and forgotten in
the other. The wrapper also owns the checks the null backend could not make
because it had no GPU: a destroy while a submission is in flight, descriptor
exhaustion, and the aliasing check above. Errors reach the `Rhi` log channel and
the recorded call returns `Status` failure; the layer never repairs a call, and
it never carries state that the frame ring would have to reset per frame.

`Exhaustive` adds a per-call trace at trace level and the leak report at
device destruction.

### Names, markers, and timestamps

Every D3D12 object already takes a `label` through `NameObject`; the increment
completes the coverage (heaps, allocators, query heaps, and the swapchain's
images) and asserts it through `ID3D12Object::GetPrivateData`, which is the only
honest way to test a name: query it back.

Pass markers come from the RHI itself: `BeginRenderPass` opens a labelled PIX
event named after the pass's attachments, and the command list's explicit
`BeginDebugLabel`/`EndDebugLabel` nest inside it. That ordering is what makes a
capture readable, and it is why the marker is emitted at the RHI level rather
than left to each caller.

Timestamps use a query heap per frame in flight, in a ring the same shape as the
command-allocation ring: frame N writes its timestamps, frame N+1 resolves frame
N's, and the results are read only after the fence that guards frame N is
reached. `Feature::TimestampQueries` is advertised only when
`CheckFeatureSupport` says the adapter has `D3D12_FEATURE_D3D12_OPTIONS3`
timestamp support, and the test reports that it skipped when a driver does not —
WARP's answer differs between driver versions, and a test that pretends to
measure pass timings on WARP would be a lie either way.

### The leak report

`ReportLeaks` walks the device's slot tables and reports every live resource:
kind, label (or `unlabelled`), size, generation, and creation order, one line
per resource, plus a count. It reports through the `Rhi` channel and returns the
count so a test can assert it.

A leak report is only worth having if it is read, so `Exhaustive` calls it at
device destruction as well, which is where a test can see it without a game to
run. The test leaks one labelled buffer and one labelled texture and asserts both
are named and that nothing else is.

### Device-lost

The removal path has three parts, in this order:

1. **Notice it.** `Submit`, `Present`, and `WaitFence` check for
   `DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_RESET`, and
   `D3D12_ERROR_DRIVER_VERSION_MISMATCH` on the call's `HRESULT` and on
   `ID3D12Device::GetDeviceRemovedReason`.
2. **Say why.** Query `ID3D12DeviceRemovedExtendedData` for the auto-breadcrumb
   and page-fault data when the interface exists, and log the reason code, the
   last submission's fence values, and the DRED breadcrumbs on the `Rhi`
   channel at error level. A removed device with no diagnostics is the worst
   version of this failure.
3. **Stay removed.** The device records the state, every later call returns
   `ErrorCode::DeviceLost` naming the original reason, and nothing tries to
   recreate itself: resource recreation is a caller's policy, and a device that
   silently recreated its objects would leave every handle the caller holds
   stale but valid-looking.

The test forces a removal rather than provoking a hang: `ID3D12Device5` exposes
`RemoveDevice` when the debug layer is on, which is the same path a real
removal takes and is available on CI. When the interface or the debug layer is
absent the test reports that it skipped the forced-removal case and still checks
the state machine — a device told it is lost stays lost — by driving the same
recording path with the fault injected through the device's fault flag, which
is `ValidationLevel::Exhaustive`-only and documented as such.

## Alternatives considered

- **Let the driver's debug layer be the validation layer.** Rejected: it is off
  in release, it fails the process rather than returning a status, and its
  messages describe D3D12 calls rather than what the RHI promised.
- **RHI-level validation per backend.** Rejected: the rule that a draw needs
  every bound group is not D3D12's, it is the RHI's, and two implementations
  drift. The null backend is the proof that one implementation can serve both.
- **A general heap allocator with defragmentation.** Rejected for this phase:
  it needs relocation and a frame boundary, and the statistics it would need are
  the same ones being added here.
- **Two allocation paths, one for `Retained` and one for `Transient`.** Rejected:
  the placement is identical and only the reuse rule differs, so a separate path
  would be two chances to get alignment wrong.
- **Timestamps read back on the frame they were written.** Rejected: reading a
  query heap the GPU may still be filling is exactly the hazard the frame ring
  exists to prevent.

## Risks

- **Fragmentation under a long frame.** First fit with no defragmentation will
  eventually refuse a request a compacting allocator would serve. `MemoryStats`
  reports the waste and the largest servable request precisely so this shows up
  as a number before it shows up as a failed frame.
- **Aliasing correctness.** The whole feature rests on the caller's promise and
  the fence comparison that checks it. If the check has a hole, the bug it lets
  through is a wrong pixel under load, which is the hardest class to attribute.
  The test plan therefore includes a deliberate early reuse, and it must fail.
- **A validation layer that is slower than the work it guards.** Checks are
  per-recorded-call and allocation-free; the layer is off by default in
  release, and `Exhaustive` is documented as a debugging level rather than a
  shipping one.
- **WARP's timestamps.** If WARP cannot answer, the feature is untestable on the
  adapter CI has. The test reports the skip, and the number the phase's report
  cites comes from the hardware adapter when the machine has one.
- **DRED availability.** `ID3D12DeviceRemovedExtendedData` needs a recent SDK
  and a debug layer. The path degrades to the reason code and the fence values,
  which is still more than a bare `HRESULT`.

## Test plan

| Test | Proves |
|---|---|
| `rhi_gpu_memory` | Sub-allocation, alignment, growth, per-class separation, dedicated heaps for oversized requests, the statistics and fragmentation numbers, and reuse of a released transient's memory. |
| `rhi_validation_errors` | The deliberate-error list: a missing barrier, a barrier with the wrong source state, a draw with an unbound group, a destroy while in flight, and a transient reuse before its fence — each caught, each naming the resource. |
| `rhi_debug_markers` | Names read back through `GetPrivateData`, markers emitted with markers enabled, and no failure when they are not. |
| `rhi_timestamps` | A written range resolves after its fence, the frequency is non-zero, and the API refuses to read a heap the GPU may still be filling. |
| `rhi_leak_report` | A leaked buffer and texture are named with their kind and size, and a device that leaks nothing reports zero. |
| `rhi_device_lost` | A forced removal reports `DeviceLost` with a reason, every later call stays failed, and a device with no removal never reports one. |
| `rhi_d3d12_conformance` | The existing D3D12 suite still passes with the validation layer in front of the backend, which is the evidence that the layer is transparent. |

## Sources

- Microsoft, *Direct3D 12 programming guide*, "Memory management" and
  "Suballocation within heaps": placement alignment, `CreatePlacedResource`,
  `ID3D12Heap` budgeting, and the resource-alignment constants.
- Microsoft, *Understanding the Direct3D 12 debug layer*: GPU-based validation,
  DRED, and `ID3D12DeviceRemovedExtendedData`.
- Microsoft, *PIX on Windows* and *RenderDoc* documentation: what a capture
  needs in order to be readable — names, markers, and pass structure.
- Khronos, *Vulkan 1.3 specification*, memory allocation and aliasing chapters:
  read for the aliasing rules this design has to state in its own terms, since
  D3D12 expresses the same hazard as "do not overlap a live resource".
- Frostbite, *FrameGraph: Extensible Rendering Architecture in Frostbite*
  (GDC 2017), read for what the frame graph will need from this allocator in
  Phase 20: transient lifetimes, aliasing legality, and a per-frame memory
  budget.

Read for the second document this phase writes, `docs/debugging.md`: it lands
with increment 4, when there are names and markers for it to describe.
