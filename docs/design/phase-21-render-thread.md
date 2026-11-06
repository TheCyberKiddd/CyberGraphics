# Phase 21 design: render thread and frame pipelining

## Goal

Give the renderer a bounded, explicit handoff from the game thread to one
render thread. The game thread publishes immutable frame snapshots without
waiting for frame execution; the render thread exclusively owns callback-driven
RHI setup, recording, resize, and teardown. Two frames in flight is the
default, while one through the RHI-supported maximum remains configurable.

The public API remains in `cyber_renderer`; it is not the stable C ABI and does
not add gameplay or product-plugin code.

## Constraints and inputs

- The Phase 21 build-guide tasks and acceptance criterion are binding.
- `docs/architecture/threading.md` requires one owner per mutable object,
  immutable published frame data, explicit shutdown sequencing, and a
  permanent single-threaded reference mode.
- `rhi::Device` supports concurrent device calls, but command-list recording
  requires one recording owner per list. Phase 21 narrows renderer-side use:
  renderer callbacks that create, record, submit, resize, or destroy RHI state
  run on the render thread. A frame's GPU lifetime is still protected by the
  existing RHI `FrameRing` and fences; CPU packet completion is not GPU
  completion.
- `platform::Thread` supplies explicit start/join and allocator-backed thread
  context. The profiler contract requires implementations used by worker
  threads to be thread-safe.
- Existing hot-pipeline jobs are brought under the ownership rule: workers
  prepare reflection and vertex layouts only; RHI shader, layout, and pipeline
  objects are materialized and retired at render-owner frame boundaries.
- No render-thread implementation may allocate from a shared, non-thread-safe
  allocator after startup.

## API and ownership

`RenderThread` is non-copyable and explicitly created from a descriptor that
names the allocator, profiler, frame-slot count, maximum copied frame-data
size, execution mode, and four non-throwing callbacks:

- initialization runs on the render owner before creation succeeds;
- frame execution receives an immutable byte span, monotonic frame index, and
  reusable CPU slot index;
- resize runs in queue order with frames and treats zero dimensions as a
  minimized surface;
- shutdown runs after accepted work drains, on the render owner.

The application context is borrowed and must outlive the thread. It may hold
the device and renderer resources, but only the callbacks may touch them.
Initialization failure is returned by creation; later callback failures are
retained and returned by `WaitIdle` or `Shutdown`. The first error is preserved
so cleanup cannot hide the operation that originally failed.

The game thread is the sole producer and lifecycle owner. Its identity is
captured at creation and checked on producer/lifecycle entry points. The
render-thread identity is available for diagnostics. A callback must not wait
for producer progress or call a blocking lifecycle operation.

## Handoff and bounded backpressure

The threaded path uses a fixed-capacity SPSC command ring. Each submitted frame
is copied into one of a bounded set of preallocated packet slots before the
queue publishes its slot index with release semantics. The render thread
acquires the command, sees the complete immutable snapshot, executes it, then
releases the slot. There are no per-frame allocations and no borrowed frame
buffer whose lifetime can race reuse.

The packet-slot count is the configured maximum number of CPU frames in flight
(two by default). A full packet set or command ring returns `ErrorCode::Busy`
instead of blocking the game thread. An oversized payload returns
`ErrorCode::OutOfRange`. Resize commands use the same ordering and bounded
queue; callers retry `Busy`. The command queue's publication wake uses
`std::atomic::wait/notify`, avoiding polling and lost condition-variable
wakeups.

This CPU backpressure is separate from GPU backpressure. Frame callbacks use
the RHI frame ring and its completion fence before reusing GPU-visible
per-frame resources.

## Ordering, modes, and lifecycle

One monotonically increasing command sequence preserves producer order across
frame and resize operations. The single-threaded mode invokes the same
callbacks, passes the same copied snapshots, observes the same ordering, and
keeps the same bounded input validation; it exists for deterministic
debugging, not as a second implementation.

Shutdown is producer-owned and non-canceling: stop accepting commands, wake
the consumer, drain every accepted frame and resize, invoke the shutdown
callback, and join. The callback is responsible for waiting on GPU fences and
destroying callback-owned RHI state in reverse dependency order. A failed
resize or frame does not silently stop later accepted commands. A callback
must not wait on the game thread, and shutdown must not be initiated from a
render callback.

## Profiling

Named scopes cover frame publication on the producer and initialization,
frame execution, resize, and shutdown on the render owner. The profiler is
borrowed and must outlive the render thread. The overlap test records both
thread IDs and coordinates an intentional overlap, rather than inferring
concurrency from elapsed time alone.

## Verification

- Validate public header self-containment, invalid configuration, payload
  bounds, `Busy` backpressure, copied-snapshot immutability, ordering,
  callback error propagation, single-thread parity, and producer ownership.
- Stress ordered resize and shutdown while frame callbacks are active and
  prove all accepted commands drain before shutdown.
- Run all repository Debug, Release, and Debug-ASan tests, structure/layer/
  formatting checks, and package/source consumers.
- Run the null-backend concurrency tests under Clang ThreadSanitizer on Linux.
  Windows-only validation cannot substitute for this race check.
- Hot-pipeline backend-object creation is intentionally on the render owner;
  driver compilation may increase render-thread work, but does not block the
  game-thread producer.

## Reading notes

- [Phase 21 reading notes](../notes/phase-21.md).
- Christian Gyrling, “Parallelizing the Naughty Dog Engine Using Fibers,”
  Advances in Real-Time Rendering in Games, 2015. The linked PDF endpoint in
  the build guide returned HTTP 404 during this phase; no undocumented details
  from the talk are claimed as implementation requirements.
- Microsoft, [Work submission in Direct3D 12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/command-queues-and-command-lists).
  The page confirms the explicit record-then-submit model and multi-threaded
  command-list submission. CyberGraphics continues to route that work through
  its own RHI.

The decisions specific to queue ownership, backpressure, snapshots, and
lifecycle are recorded in [ADR 0024](../adr/0024-render-thread-frame-pipeline.md).
