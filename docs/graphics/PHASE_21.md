# Phase 21 — Render thread and frame pipelining

Phase 21 adds bounded producer-to-render-owner frame handoff to
`cyber_renderer`. The game thread publishes copied immutable frame packets to a
single render owner; ordered resize requests and shutdown share the same queue.
The default is two CPU packet slots, and a permanent single-threaded mode
exercises the same callbacks and packet rules.

**Status:** complete. Implementation and validation evidence are recorded in
the [Phase 21 report](../reports/phase-21.md).

## Public API

Include
[`render_thread.h`](../../engines/cyber_renderer/include/cyber/renderer/render_thread.h).
Provide an allocator and a non-throwing frame callback; optional callbacks
initialize and tear down render-owned state and apply ordered resizes:

```cpp
cyber::renderer::RenderThreadDesc desc{};
desc.allocator = &allocator;
desc.renderFrame = &RenderFrame;
desc.initialize = &InitializeRenderer;
desc.resize = &ResizeRenderer;
desc.shutdown = &ShutdownRenderer;

auto created = cyber::renderer::RenderThread::Create(desc);
if (!created)
{
    return;
}
auto renderThread = std::move(created.Value());

const auto submitted = renderThread.SubmitFrame(frameBytes);
if (!submitted)
{
    // Retry ErrorCode::Busy or handle the reported error.
}
```

`SubmitFrame` copies bytes into preallocated storage before publication. Queue
or packet saturation returns `ErrorCode::Busy`; oversized packets return
`ErrorCode::OutOfRange`. `WaitIdle` and `Shutdown` return the first callback
failure. CPU callback completion does not mean GPU completion: per-frame GPU
resources still require `rhi::FrameRing` fences.

## Ownership and decisions

- The creating game thread is the sole producer and lifecycle owner.
- Initialization, frame, resize, and teardown callbacks own callback-managed
  RHI objects and run on the render thread, except in explicit single-threaded
  mode.
- Background hot-pipeline jobs prepare CPU reflection/layout data only; RHI
  object creation and destruction move to the render owner.
- A fixed SPSC command ring and preallocated copied packets provide bounded
  memory without per-frame allocation or producer waits.
- Shutdown drains accepted commands before render-owned teardown and thread
  join.

See [ADR 0024](../adr/0024-render-thread-frame-pipeline.md), the
[design note](../design/phase-21-render-thread.md), and the
[reading notes](../notes/phase-21.md). ADR 0024 remains proposed pending owner
review.

## Validation

The null-backend tests cover callback ownership, producer/consumer overlap
through profiler scopes, copied-packet immutability, ordering, queue and packet
backpressure, resize/shutdown draining, callback errors, invalid input,
single-thread parity, and stress while frame work is active. The exact commands
and results are in the [Phase 21 report](../reports/phase-21.md).

## Known gaps

- The concurrency proof uses the null backend; no native GPU workload or
  hardware performance claim is part of this phase.
- The Clang 21 TSan preset disables warnings-as-errors because existing,
  unchanged RHI/renderer code emits warnings under that compiler. Other
  warnings remain visible, and the Windows Debug, Release, and Debug-ASan
  presets retain warnings-as-errors.
- Pipeline creation on the render owner may increase render-thread time, while
  the producer remains independent.
