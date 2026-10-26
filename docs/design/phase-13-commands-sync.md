# Phase 13 — Command lists, synchronization, and the swapchain

Date: 2026-10-26
Status: approved
Related: ADR 0013 (RHI object model), ADR 0016 (command recording and
synchronization), [`cyber_rhi` contract](../../engines/cyber_rhi/docs/MODULE.md),
[Phase 10 design note](phase-10-rhi.md), [Phase 12 design note](phase-12-pipelines.md)

## What this phase builds

Phase 10 to 12 produced objects that can be created, described, and bound, but
nothing has ever been *executed*. Phase 13 adds the execution model:

- **Command lists.** A list is a recorded, immutable program: render passes,
  state setup, draws, dispatches, copies, clears, barriers, push constants, and
  debug labels.
- **Render passes.** The attachment set, its load and store behaviour, and the
  pass extent every pipeline in it must match.
- **Submission.** One call that hands a set of finished lists to a queue, waits
  on fences, and signals fences when the work completes.
- **The frame ring.** A fixed number of frames in flight, each with a timeline
  value, so a frame's resources are never reused before the GPU finished the
  frame that used them.
- **Deferred destruction.** Objects are retired against a fence value and
  released when the timeline passes it.
- **The swapchain.** A ring of presentable images the device owns but the
  caller's surface backs, with acquire, present, resize, and present modes.

The done-when line is two tests: a missing barrier has to be caught by
null-backend validation, and a frame-ring test has to prove no resource is
reused before its fence. Both are the reason the phase exists: every later
phase submits work, and a renderer that gets synchronization wrong produces
corruption that only appears on one vendor's driver.

## Why recording is a value class and not a handle

Resources are handles because a caller stores thousands of them and every
operation goes through the device. A command list is different: it is created,
recorded against, and thrown away in a tight scope, and every recording call
needs the list's own state (is it recording, what pipeline is bound, is a pass
open). Making it a class with a handle inside gives three things a free
function table on `Device` cannot:

1. the recording state machine is impossible to skip — `Draw` on a list that is
   not recording is a return value, not a crash;
2. the CPU-side state that only the caller needs (current pipeline kind, pass
   extent, label depth) lives with the list instead of in the device;
3. a moved-from list is empty and rejects every call instead of recording into
   a list another thread now owns.

The identity is still a `CommandListHandle`, generation counted, so submission
takes a plain handle and a stale handle is reported rather than resolved.

## The recording state machine

```text
        Begin()                 End()
Initial --------> Recording --------> Ended
   ^                  |                 |
   |    Reset()       |    Begin()      |
   +------------------+-----------------+
```

- `Initial`: created, nothing recorded.
- `Recording`: exactly one thread records. Every command is validated against
  this list's own tracked resource states.
- `Ended`: immutable. Submission accepts only this state, and only a list of
  the queue class it was created for.

`Begin` from `Ended` is legal and resets the recorded contents: D3D12 requires
`Reset` on the allocator and a list that is still pending cannot be reset, which
the backend enforces on its own side.

## Barriers, and where state is tracked

Synchronization is explicit. Every barrier names its source state and its target
state, and the source must equal what the backend tracks; `Undefined` as a
source discards contents and is the only value that bypasses the check.

Three rules shape the model:

1. **State is tracked per subresource.** A texture barrier resolves its range
   and transitions every `(mip, layer)` in it. Buffers have one state.
2. **A batch is all or nothing.** Every barrier in a call is validated before
   any is applied, so a caller can fix one descriptor and resubmit without
   guessing which half took effect.
3. **Recording validates against the list's own view.** During recording the
   list keeps a shadow of the states it expects — seeded from the device's
   tracked state on first touch — so `Barrier` followed by `Draw` validates in
   order without mutating the device. The null backend commits the shadow to
   the device when the list *ends*, because it has no execution timeline; a
   real backend commits it when the list runs.

The shadow is what makes a missing barrier a record-time error with the exact
resource named:

```text
record: set vertex buffer(vb)          -> requires vb in vertex_buffer  (FAILS, undefined)
record: barrier(vb, undefined -> vertex_buffer)
record: set pipeline(p), draw(3)       -> ok
```

## What each command validates

This is the contract the null backend enforces, and therefore the contract a
real backend must also honour:

| Command | Rules |
|---|---|
| `Begin` | no pass open, state is `Initial` or `Ended` |
| `BeginRenderPass` | no other pass open; one to eight color attachments, no duplicates; every attachment used as `RenderTarget`/`DepthStencil`; consistent format-compatible extents and sample count; depth rules from `ValidateRenderPassDesc`; every attachment transitions to `RenderTarget` (or `DepthRead`/`DepthWrite`) |
| `SetPipeline` | live handle; a compute pipeline cannot be bound inside a pass and a graphics pipeline cannot be bound outside one |
| `SetBindGroup` | a pipeline is bound; the slot is used by the pipeline layout; the group's layout equals the layout's group for the slot |
| `SetVertexBuffer` | a pipeline is bound; the slot is below the pipeline's binding count; the buffer has `Vertex` usage and is in `VertexBuffer` |
| `SetIndexBuffer` | a pipeline is bound; the buffer has `Index` usage and is in `IndexBuffer`; the offset is a multiple of the index size and inside the buffer |
| `SetViewport` / `SetScissor` | a pass is open; inside the pass extent; `minDepth <= maxDepth` |
| `Draw` | a graphics pipeline is bound, a pass is open, every vertex binding the pipeline declares is bound, every group slot the layout uses is bound, every bound resource is in a compatible state, arguments non-zero and within limits |
| `DrawIndexed` | all of the above plus a bound index buffer |
| `Dispatch` | a compute pipeline is bound, no pass open, every group slot bound, every resource compatible |
| `CopyBuffer` | both buffers live, `CopySource`/`CopyDestination`, ranges inside, no overlap, states `CopySource`/`CopyDestination` |
| `CopyBufferToTexture` / `CopyTextureToBuffer` | pitch and extent rules, block alignment, row pitch at least the row's byte size, states `CopySource`/`CopyDestination` |
| `ClearColorTexture` | `RenderTarget` usage, state `RenderTarget`, format is not depth |
| `ClearDepthStencilTexture` | `DepthStencil` usage, state `DepthWrite`, format is depth |
| `Barrier` | as `Device::Transition`, against the list's shadow |
| `PushConstants` | a pipeline is bound; the range is covered by one range of the pipeline layout with matching stages |
| `BeginDebugLabel` / `EndDebugLabel` | balanced; an unbalanced label at `End` is an error |

## Submission

```cpp
struct SubmitDesc
{
    QueueKind queue = QueueKind::Graphics;
    memory::Span<const CommandListHandle> commandLists{};
    memory::Span<const FenceSignal> signals{};
    memory::Span<const GpuWait> gpuWaits{};
    const char* label = nullptr;
};
```

- `gpuWaits` are the queue's own waits: the queue does not begin until each
  named fence reaches its value. This is D3D12's `ID3D12CommandQueue::Wait` and
  Vulkan's timeline semaphore wait, so a cross-queue dependency is expressed
  without the CPU blocking.
- `signals` are values the queue reaches when everything submitted before this
  call has finished.
- A list recorded for one queue class is rejected on another, because D3D12
  command list types are not interchangeable and Vulkan's queues are not either.
- Duplicate signals on one fence with decreasing values are accepted and
  normalised, because a timeline never moves backwards.

The submission itself is one function call per batch, which is the unit a
render thread will later produce. Nothing in `SubmitDesc` allocates.

## Frame lifecycle

```text
frame N
  |
  |-- FrameRing::BeginFrame(device)
  |     waits until frame N - framesInFlight has completed
  |     returns slot = N % framesInFlight and the value to signal
  |
  |-- record list A: pass 1            (transitions, draws)
  |-- record list B: pass 2
  |
  |-- Device::Submit({ graphics, [A, B], signal: fence = value })
  |     GPU begins after gpuWaits; signals the fence when done
  |
  |-- present
  |
frame N + 1
  |
  |-- BeginFrame waits on value-3, not on value-2 and value-1, because two other
  |   frames may still be in flight
  |
  |-- resources touched by frame N-3 may now be retired:
  |     RetirementQueue::Retire(texture, fence, value) at submission time
  |     RetirementQueue::Collect(device) at the start of every frame
```

The invariant the frame ring enforces is the one that matters: **a resource is
not written by frame N+1 while frame N is still reading it**. With three frames
in flight the CPU is up to three frames ahead of the GPU, and each slot's fence
value is the only thing standing between that and a torn frame.

## Swapchain

```cpp
struct SwapChainDesc
{
    SurfaceDesc surface{};
    Format format = Format::BGRA8UnormSrgb;
    u32 imageCount = 2;
    PresentMode presentMode = PresentMode::Fifo;
    const char* label = nullptr;
};
```

- `Fifo` waits for vblank (D3D12's `DXGI_SWAP_EFFECT_FLIP_DISCARD` with interval
  1, Vulkan's `VK_PRESENT_MODE_FIFO_KHR`), `Mailbox` replaces, `Immediate`
  presents without waiting and tears, which is why it needs
  `Feature::SwapchainTearing`.
- At least two images, at most `kMaxSwapChainImages`. `Mailbox` needs three:
  with two, a present would have to wait for the image being displayed.
- `AcquireNextImage` returns the image's texture and a view over it, and fails
  if an image is already acquired: presenting is what releases it.
- `Resize` recreates the images, invalidating every handle a caller kept, and
  resets the acquire state.
- The images are device-owned and permanently in `Present` state outside a
  frame; a pass transitions them to `RenderTarget` and back, exactly as on a
  real swapchain.

## The null backend's execution model

The null backend has no GPU, so submission *is* execution: `Submit` walks the
lists, validates every recorded command against the device's tracked state,
applies the transitions the list recorded, and signals the requested fences
before returning. That is a deliberate choice:

- every rule a real backend needs is exercised by every test in the project;
- a fence is always reached after `Submit`, so `FrameRing` never blocks in a
  test unless a test deliberately withholds a signal;
- the cost is that a test cannot observe a pending frame. The frame-ring test
  proves the *policy* (which value each slot waits for) rather than measuring
  overlap, and Phase 21 measures overlap on the real backend.

## What this phase deliberately does not do

- No automatic barrier insertion. The render graph in Phase 20 owns that, and it
  needs the explicit model underneath it to be trustworthy first.
- No secondary command lists or bundles.
- No queries, timers, or occlusion. Timestamp queries arrive in Phase 15.
- No multithreaded recording of one list. Two lists on two threads is fine and
  is what Phase 21 will use.

## Alternatives considered

**An implicit API that tracks dependencies and inserts barriers.** Rejected:
the phase's done-when is that a *missing* barrier is caught, which only means
something if barriers are the caller's job. The render graph will generate them
in Phase 20 from resource declarations.

**Recording through `Device` free functions.** Rejected in the section above.

**A per-frame descriptor of work instead of command lists.** Rejected: it puts
the backend in charge of ordering, which is exactly the abstraction a render
graph needs to control.

**Deferred destruction inside the backend.** Rejected for Phase 13: the
retirement queue needs a fence and a policy (how long to keep, when to collect),
and both are the renderer's decisions, not the driver wrapper's. The backend
only destroys what it is told to destroy.
