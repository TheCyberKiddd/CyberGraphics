# 0016 — Recorded command lists, explicit barriers, and timeline submission

- **Status:** accepted
- **Date:** 2026-10-26
- **Deciders:** @TheCyberKiddd
- **Related:** Phase 13, [design note](../design/phase-13-commands-sync.md),
  ADR 0013, ADR 0014

## Context

Phase 10 to 12 created objects; Phase 13 has to run them. The three candidate
APIs agree on almost nothing here:

- D3D12 records into a command list owned by a command allocator, which the
  application resets per frame; a list can be executed many times and stays alive
  until its allocator is reset.
- Vulkan records into a command buffer from a pool, with primary and secondary
  levels and its own reset rules.
- WebGPU records into an encoder and finishes it into a command buffer that is
  consumed by exactly one submit.

All three have explicit barriers of some kind (D3D12 legacy and enhanced
barriers, Vulkan pipeline barriers and layout transitions, WebGPU none at all),
and all three have a way to wait for work to finish: fences, timeline
semaphores, or `onSubmittedWorkDone`.

The constraints from `AGENTS.md` narrow the answer further: no exceptions, no
RTTI, no global mutable state, allocation only through an explicit allocator, and
a C ABI plugin boundary in Phase 25 that has to express whatever is chosen here.

## Decision

1. **A command list is a move-only value class wrapping a generation-counted
   handle.** The class owns the recording state machine and the CPU-side state a
   caller needs; the handle is the identity for submission. `End()` produces an
   immutable list.
2. **Barriers are recorded, explicit, and validated in order against a per-list
   shadow of resource state.** The shadow is seeded from the device's tracked
   state on first touch. A command that reads or writes a resource in the wrong
   state fails at record time with the resource and the missing transition
   named.
3. **A render pass is an explicit begin/end pair with load and store ops.** The
   pass resolves its extent and sample count from its attachments, and every
   pipeline bound inside it must match those. Attachments are transitions the
   backend inserts, not something a caller writes by hand.
4. **Submission is one call per batch.** `SubmitDesc` carries the queue, the
   lists, the fence values to signal, and the GPU-side waits. Nothing in it
   allocates, and it is the unit a render thread will produce in Phase 21.
5. **Cross-queue dependencies are timeline waits, not CPU waits.** `GpuWait`
   names a fence and a value the queue must not pass. `Device::WaitFence` is for
   the CPU and is never used to order GPU work.
6. **Frames in flight are a policy object, not a device setting.** `FrameRing`
   owns the slot index and the fence values; the device owns the fence. A caller
   that wants one frame in flight constructs a ring with one slot.
7. **Destruction is deferred by a queue the renderer owns.** `RetirementQueue`
   holds packed handles with the fence value that makes them safe to destroy and
   releases them when the timeline passes it. The backend destroys immediately
   when asked; deferral is a policy, and policy belongs above the driver wrapper.
8. **The swapchain owns its images and hands them out one at a time.**
   `AcquireNextImage` fails while an image is outstanding, `Present` releases it,
   and `Resize` recreates the images and invalidates every handle over them.

## Consequences

- A missing barrier is a test failure with a message, not a corrupted frame. The
  cost is a linear search over the list's shadow per touched resource, which is
  bounded and only paid while recording.
- The shadow is a fixed 512-entry budget per list. A list that touches more
  distinct subresources reports `OutOfRange` instead of silently dropping
  tracking; a real backend does not need the shadow at all because its driver's
  validation layer or GPU itself catches the error.
- Recording is single-threaded per list by contract. Two threads with two lists
  is the supported parallel path, and Phase 21 depends on it.
- A list recorded on one queue class cannot be submitted to another. That is
  stricter than Vulkan (which allows a graphics list on a compute queue with
  caveats) and exactly D3D12's rule; strictness here prevents a backend from
  having to carry a compatibility matrix.
- `FrameRing` blocks the CPU until the oldest in-flight frame completes. That is
  the correct behaviour for a fixed ring and the reason the ring size is a
  caller's choice rather than a constant.
- `RetirementQueue` needs a live device to collect. If the device dies first, the
  queue leaks handles that the device already released; the destructor therefore
  never touches the device, and `Flush` is explicit.
- The swapchain images live in the same texture table as every other texture, so
  a stale image handle fails to resolve after `Resize` instead of aliasing a new
  image. The cost is one table insert per image per resize.
- The null backend applies a list's transitions when the list ends rather than
  when it runs, because it has no timeline. A test can therefore observe state
  that a real GPU would only reach later; Phase 14 records the difference in its
  conformance notes.

## Alternatives

- **An implicit dependency graph that inserts barriers.** Rejected at the RHI:
  the render graph in Phase 20 generates barriers from declared reads and writes,
  and it needs the explicit model to sit on. It also makes "a missing barrier is
  caught" impossible to test, which is this phase's done-when line.
- **Recording through free functions on `Device`.** Rejected: it scatters the
  list's own state (recording, pipeline, pass) into the device, and a call on a
  list that is not recording would have to be checked by hand at every site.
- **One `ExecuteCommandLists` per list.** Rejected: a batch is the unit the
  driver optimises for, and one call per list would double the submission cost
  measured in Phase 19.
- **A binary fence instead of a timeline.** Rejected in ADR 0013 and again here:
  a frame ring needs to ask "is frame N done?", which a binary signal cannot
  answer for more than one frame.
- **Deferred destruction inside the backend, keyed by its own internal
  timelines.** Rejected: the backend does not know how long a caller wants to
  keep an object, and a policy in the wrong layer becomes untestable in the null
  backend, which has no asynchrony to defer against.
- **Present-mode emulation in the RHI** (mapping `Mailbox` to `Fifo` when the
  backend lacks it). Rejected: silently changing latency is worse than reporting
  `Unsupported`, and the caller is the one that knows whether tearing is
  acceptable.

## Follow-up

- Phase 14 implements the same contract on D3D12, including command allocators
  per frame, a pipeline signature per layout, and flip-model present.
- Phase 15 adds the validation layer that this phase's null backend proves is
  worth having, plus timestamp queries for per-pass timing.
- Phase 20 replaces the hand-written barriers with a graph, using `Barrier` as
  its only output.
- Phase 21 moves recording to a render thread and makes the frame ring the
  contract between threads.
