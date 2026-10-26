# Phase 13 report: command lists, synchronization, and the swapchain

Date: 2026-10-26
Machine: Windows 11, 13th Gen Intel Core i7, MSVC 19.44, CMake 3.31
Design note: [phase-13-commands-sync.md](../design/phase-13-commands-sync.md)
ADR: [0016](../adr/0016-command-lists-and-synchronization.md)
Contract: [`cyber_rhi`](../../engines/cyber_rhi/docs/MODULE.md)

## Done-when

> The null backend's validation catches a missing barrier in a test, plus
> command recording, fences, barriers, multiple frames in flight, and a
> swapchain that can be resized.

Proved by seven new test binaries with 46 cases between them:

- **A missing barrier is caught.** `rhi_missing_barrier` records a pass that
  samples a texture written by an earlier pass without a transition, and the
  backend fails the submission with `ResourceAlreadyInUse` naming the resource
  and both states. The same test then inserts the transition and the identical
  recording submits clean. The rule is tracked per resource across the whole
  list, not per pass, so a read-after-write in the same list is caught.
- **Multiple frames in flight.** `rhi_frame_ring` proves the ring recycles a
  slot only after its fence value is reached, that a submission waits on a slot
  still in use, and that a destroyed ring releases its slots.
- **Swapchain.** `rhi_swapchain` covers creation, format and present-mode
  negotiation, resize invalidating the old images and handing out new ones,
  acquire/release ordering, and a stale image handle after the next acquire.
- **Retirement.** `rhi_retirement` proves a resource is destroyed only when the
  fence value that guarded it is reached, and that teardown drains the list.
- **Coverage.** 74 of 74 tests pass in debug, release, shipping, and ASan.

## What was built

| Header | Contents |
|---|---|
| `render_pass.h` | `LoadOp`, `StoreOp`, `ClearColorValue`, `ClearDepthStencilValue`, `ColorAttachment`, `DepthStencilAttachment`, `RenderPassDesc`, and the validation and naming helpers. |
| `command_list.h` | `CommandList` with `Begin`/`End`/`Reset`, graphics and compute passes, draws, dispatches, copies, buffer and texture barriers, push constants, bind groups, viewport and scissor, and the recording counters a frame report reads. |
| `submission.h` | `SubmitDesc`, `SubmitInfo`, `QueueKind` selection, and the wait/signal pair a submission carries. |
| `swapchain.h` | `SwapchainDesc`, `PresentMode`, `SwapchainInfo`, acquire and present, resize, and the image handles a frame consumes. |
| `frame_ring.h` | `FrameRing` — N frames in flight, each with its own command list, fence value, and the point at which a slot may be reused. |
| `retirement.h` | `RetirementQueue` keyed by fence value: `Retire(handle, value)`, `Drain(completedValue)`, and teardown. |

`handle.h` gains `CommandListHandle`, `SwapchainHandle`, and `FenceHandle` reuse
from Phase 10. `backend.h` gains twenty-two entries; `device.h` and `device.cpp`
add the matching methods, and the null backend implements recording, validation,
submission, the swapchain state machine, the frame ring, and the retire list.

The command list validates every recording call against the device's object
table, so a stale pipeline, bind group, buffer, or texture handle is reported at
record time rather than at submit. Resource state transitions are tracked in a
per-list map from resource to current state, which is what makes the
missing-barrier rule exact rather than advisory.

### What the interface implements today

A frame can be recorded end to end against the null backend: begin a list, begin
a pass, bind a pipeline and a group, set a viewport, draw, transition to a
sampled state, end the pass, end the list, submit with a wait and signal value,
present through the swapchain, and retire the frame's transient resources. No
pixels are produced — that is Phase 14's D3D12 backend — but every call, state,
and lifetime rule is exercised.

### Example

```cpp
auto frame = device.BeginFrame();          // acquires a swapchain image
CommandList list = device.BeginCommandList(frame.commandList);

cyber::rhi::RenderPassDesc pass{};
pass.colorAttachments = cyber::memory::Span<const cyber::rhi::ColorAttachment>(&color, 1);
pass.hasDepthStencil = true;
pass.depthStencil.view = depthView;
device.BeginRenderPass(list, pass);

device.BindPipeline(list, pipeline);
device.BindGroup(list, 0, group);
device.SetViewport(list, viewport);
device.Draw(list, 36, 1, 0, 0);
device.EndRenderPass(list);

const cyber::rhi::Barrier barrier[]{colorTransitionToSampled};
device.Barrier(list, cyber::memory::Span<const cyber::rhi::Barrier>(barrier, 1));
device.EndCommandList(list);

cyber::rhi::SubmitDesc submit{};
submit.commandLists = cyber::memory::Span<const cyber::rhi::CommandListHandle>(&list, 1);
submit.signalValue = frame.fenceValue;
device.Submit(submit);
device.Present(frame.swapchain);
device.Retire(frame.transient, frame.fenceValue);
```

## Decisions

- **Recording is explicit, validation is eager.** See ADR 0016. Every rule that
  can be checked while recording is checked there, because a frame that fails at
  submit has already spent the CPU time.
- **State is per list, not per pass.** A read-after-write inside one list is as
  wrong as one across two, and the per-list map is the only place both are
  visible.
- **A submission carries exactly one wait and one signal value.** That is what
  a timeline fence can express on all three APIs without an array of semaphores.
- **The frame ring owns the command list, not the caller.** A slot's list is
  reset when the slot is reused, so a caller cannot record into a list a GPU
  still reads.
- **The retire list is keyed by fence value rather than by frame index.** A
  resource guarded by frame 7's value is released when value 7 is reached, even
  if the ring has already moved on.
- **The swapchain never owns a window.** The surface is a borrowed native handle,
  unchanged from ADR 0013; resize is a call, not a callback.
- **ASan found a real defect in the new tests.** `CG_EXPECT_EQ` binds a reference,
  so `CG_EXPECT_EQ(ClearDepthStencilValue{}.depth, 1.0f)` read a member of a
  temporary after its lifetime ended. The test now names the value. The macro is
  correct — a reference is what makes a handle or a struct comparison cheap — and
  the lesson is recorded in the test.

## Evidence

| Check | Result |
|---|---|
| `scripts/build.ps1 -Preset debug` | pass |
| `scripts/build.ps1 -Preset release` | pass |
| `scripts/build.ps1 -Preset shipping` | pass |
| `scripts/build.ps1 -Preset debug-asan` | pass |
| `ctest` debug / release / shipping / debug-asan | **74/74 each** — 67 existing plus 7 new binaries |
| `scripts/check_structure.py` | 16 modules, 1 contract |
| `scripts/layer_check.py` | 16 modules |
| `scripts/check_format.py` | 252 files |
| `scripts/verify_package.ps1 -Preset debug` | installed consumer configures, links, runs — exit 0 |
| source consumer | configures, links, runs — exit 0 |

The ASan runtime DLL still needs `VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64` on
`PATH`; with it, 74/74 pass.

New test binaries:

| Test | Cases | What it proves |
|---|---|---|
| `rhi_render_pass` | 7 | Color and depth attachments, load and store rules, clear values, the reverse-Z depth default, and every rejection: zero attachments, a depth attachment without a format, a depth pass with no clear and no load, and a read-only depth attachment used as a target. |
| `rhi_command_list` | 12 | Begin/end ordering, double begin, recording after end, illegal state on the wrong queue, viewport and scissor bounds, push-constant range checks, bind-group slot and layout agreement, copies across buffers and textures, and reset semantics. |
| `rhi_submit` | 5 | A submission with no list, a list not ended, a list recorded for the wrong queue, the wait and signal pair reaching the fence, and a stale list handle. |
| `rhi_swapchain` | 5 | Creation, negotiation, acquire/present order, resize invalidating images, and image handles going stale. |
| `rhi_frame_ring` | 4 | Slot recycling gated on the fence value, waiting on a slot still in use, per-slot list reset, and teardown. |
| `rhi_retirement` | 5 | Retirement gated on the fence value, ordering within a value, draining, and teardown. |
| `rhi_missing_barrier` | 8 | The done-when case: a sampled texture written without a transition fails with `ResourceAlreadyInUse`, the transition fixes it, same-list read-after-write, write-after-read, and a buffer written while read. |

Allocator accounting is asserted after recording, submission, resize, and
teardown: `HeapAllocator::Stats().used` returns to zero, so the copied barriers
and attachment arrays are released with their objects.

The installed and source consumers now record a full frame through the exported
`cyber::rhi` target: a render pass with a color and a depth attachment, a bound
pipeline and group, a draw, a sampling transition, a submit with a signal value,
a present, and a retirement, then destroy everything.

## Known gaps

- **The null backend produces no image.** It validates and counts; Phase 14's
  D3D12 backend is the first to rasterize.
- **`DeviceLost` still has no producer.** It arrives with D3D12.
- **No command pool or allocator is exposed.** The ring recycles lists
  internally, which is the interface the three APIs can share; Phase 34 revisits
  it if a backend needs per-thread pools visible.
- **The swapchain is not measured.** Present modes are validated, not timed.
- **Barrier batching is the caller's job.** The render graph that generates
  barriers is Phase 18; this phase proves the interface it generates from.
- **Static error messages**, unchanged from Phase 11: formatted diagnostics wait
  for Phase 15's error sink.

## Sources

- Microsoft, *Direct3D 12 programming guide*: command lists, command allocators,
  fences, resource barriers, and the DXGI swapchain.
- Khronos, *Vulkan 1.3 specification*: command buffers, timeline semaphores,
  pipeline barriers, and `VK_KHR_swapchain`.
- W3C WebGPU candidate recommendation: command encoders, passes, and the
  implicit synchronization model this phase deliberately does not adopt.
- Fiedler, *A Trip Through the Graphics Pipeline* (2011), parts 1–3: the
  command processor and where a barrier has to land.
- Akenine-Möller et al., *Real-Time Rendering*, 4th edition: double buffering and
  frames in flight.

## What the next phase needs from this one

- `DeviceInterface` is still the only backend surface; Phase 14 implements the
  same table for D3D12 without adding entries to it.
- The recording API is the shape a D3D12 command list wrapper fills in, one call
  per entry, with the same validation already done upstream.
- Phase 11's resource states map to `D3D12_RESOURCE_STATES` one to one, and the
  missing-barrier test is the conformance case both backends must pass.
- The frame ring is what Phase 14's upload ring and descriptor-heap rotation
  hang off: one slot per frame in flight.
- The retire list is where Phase 15's leak report reads its pending count.
