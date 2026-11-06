# 0024 — Render-thread ownership and bounded frame handoff

- **Status:** proposed
- **Date:** 2026-11-06
- **Deciders:** @TheCyberKiddd
- **Related:** Phase 21, [design note](../design/phase-21-render-thread.md),
  [threading model](../architecture/threading.md),
  [`cyber_renderer` contract](../../engines/cyber_renderer/docs/MODULE.md)

## Context

Phase 21 needs a game/render thread boundary that can overlap frame production
and render work without exposing mutable game-thread containers or making the
game thread wait on ordinary frame submission. The renderer already has
allocator-backed thread support, RHI GPU frame rings, and a profiler interface,
but no renderer-owned CPU command queue or copied frame-packet lifetime.

## Decision

1. Keep the public entry point in `cyber_renderer`. A `RenderThread` descriptor
   supplies non-throwing initialize, frame, resize, and shutdown callbacks;
   the application context is borrowed for the object's lifetime. Callback
   code is the sole renderer-side place that may touch its RHI device and
   render-owned objects.
2. Make the game thread the single producer and lifecycle owner. Capture and
   validate its thread identity. A dedicated consumer thread handles callbacks
   in producer order; the permanent single-threaded mode uses the same
   callbacks and snapshot rules inline.
3. Use a fixed-capacity SPSC command ring and preallocated per-frame packet
   slots. Copy frame bytes before publishing the queue entry. Release a slot
   only after its callback returns. This gives the render thread immutable
   bytes, bounded memory, no per-frame allocator access, and unambiguous reuse.
4. Default to two CPU frames in flight, with one through the RHI maximum
   configurable. Distinguish packet-slot availability from GPU completion:
   GPU-visible resources continue to use `rhi::FrameRing` and fences.
5. Apply bounded backpressure rather than blocking frame producers. Return
   `Busy` for a full queue/packet set and `OutOfRange` for an oversized packet.
   Resize is ordered with frames and is retryable on `Busy`; zero-size resize is
   valid for minimized surfaces.
6. On shutdown, reject new work, drain accepted commands, run the shutdown
   callback, and join. The callback waits for GPU completion and destroys
   callback-owned resources. The render callback may not wait for producer
   progress or initiate blocking lifecycle operations.
7. Record producer publication and consumer callback profiler scopes. Keep
   callback failures visible: startup errors fail creation, and the first
   asynchronous error is returned by `WaitIdle`/`Shutdown` without being
   replaced by a later cleanup error.
8. Restrict hot-pipeline worker jobs to CPU-only reflection and vertex-layout
   preparation. Create shaders, reflected RHI layouts, and pipeline objects at
   the render-owner frame boundary; destroy them from that owner as well.

## Consequences

### Positive

- Frame payload lifetime is owned by a bounded copy, not a game-thread pointer
  or a mutex protecting mutable scene state.
- The game thread can continue producing until the configured packet slots are
  occupied; saturation is an explicit, observable result.
- Resize and shutdown participate in the same serialization order as frame
  work, making their effects and drain behavior deterministic.
- The single-threaded path remains a comparable reference implementation.
- Existing RHI frame-ring semantics remain authoritative for GPU resource
  reuse; CPU callback completion cannot be mistaken for GPU completion.
- Worker jobs no longer call the RHI device. Backend shader/pipeline creation
  happens on the render owner, so expensive driver work can lengthen a render
  callback while the game-thread producer continues.

### Negative

- The fixed frame-data capacity must be chosen by the application; larger
  snapshots require a deliberate descriptor change and startup allocation.
- Callers must retry `Busy` and must ensure borrowed callback state and
  profiler implementations outlive the render thread.
- One render consumer serializes resize and all frame callbacks; parallel
  command-list recording and multi-queue scheduling remain separate concerns.

## Alternatives considered

| Alternative | Why not |
|---|---|
| Share live game-thread state behind a renderer-wide mutex | It allows ownership ambiguity, can stall frame production, and violates the one-owner rule. |
| Queue raw pointers to caller frame data | Lifetime and mutation safety would depend on every caller observing a separate completion protocol. |
| Allocate/copy a variable-size packet on every frame | It introduces allocator contention and failure points on the frame path. |
| Spin while the queue is empty or full | It burns CPU and turns backpressure into an implicit wait. |
| Treat CPU callback completion as GPU completion | Submitted GPU work can outlive the CPU callback; the RHI fence and frame ring remain required. |
| Stop immediately and discard queued work | It makes resize/shutdown nondeterministic and can destroy resources still referenced by accepted frames. |

## References

- Christian Gyrling, “Parallelizing the Naughty Dog Engine Using Fibers,”
  Advances in Real-Time Rendering in Games, 2015. The PDF endpoint was
  unavailable during this phase; the implementation does not attribute
  unsupported specifics to the talk.
- [Work submission in Direct3D 12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/command-queues-and-command-lists),
  Microsoft Learn.
- [Phase 21 design note](../design/phase-21-render-thread.md).
