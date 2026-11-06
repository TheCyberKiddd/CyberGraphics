# Threading model

This document describes the threading model introduced by Phase 5 (jobs), Phase
21 (render thread), and Phase 22 (scene proxies). The rules also apply to any
future code that creates a thread.

## Threads

| Thread | Owns | May touch |
|---|---|---|
| Game thread | Gameplay state, frame input, high-level scene updates | Public engine and renderer API; the command queue producer side |
| Render thread | RHI objects, renderer scene, render graph, command lists | RHI, renderer, asset handles that are immutable for the frame |
| Worker threads | Jobs with explicit dependencies | Only the data the job declares; never RHI objects unless the job is render-thread work |
| Tools | Their own state | Public APIs only |

## Rules

1. **One owner per mutable object.** If two threads need it, pass immutable
   snapshots or send commands through a queue.
2. **No hidden globals.** Contexts are explicit: `Device`, `Frame`,
   `Allocator`, `JobSystem`, `Renderer`.
3. **Published frame data is immutable.** `RenderThread::SubmitFrame` copies
   the producer's bytes before returning. The producer may reuse its source
   storage after return; the render callback sees an immutable copy valid for
   that callback only.
4. **Scene proxies follow the frame queue.** The game thread writes bounded
   proxy commands into `SceneCommandWriter`, seals them, submits the bytes to
   `RenderThread`, and commits only after acceptance. The render callback
   applies commands to its exclusively owned `Scene`; workers only read stable
   scene records during culling.
5. **Renderer RHI access belongs to the render thread.** The render thread
   creates, queries, records, submits, resizes, and destroys renderer-owned RHI
   objects. Worker jobs prepare CPU-only data and send RHI work through the
   render queue.
6. **Jobs declare dependencies.** A job never spins waiting for work that
   cannot run; it returns and lets the scheduler continue.
7. **Shutdown is a sequence, not a race.** Stop accepting work, drain the
   queue, wait for fences, destroy in reverse order.
8. **Resize and device-lost are normal paths.** They are tested, not treated as
   exceptional.

## Frame pipeline

Target model for Phase 21:

```text
frame N-2        frame N-1          frame N
  GPU executing    CPU recording       CPU building
  fence pending    commands queued     frame packet
```

Two or three frames in flight, configurable. A single-threaded mode is kept
permanently for debugging; it is not a temporary scaffold.

## Synchronization vocabulary

- **Fence**: GPU or CPU completion signal.
- **Barrier**: resource state or layout transition, expressed in RHI terms.
- **Semaphore**: queue-to-queue or acquire/present dependency (backend detail).
- **Frame ring**: per-frame allocators, descriptor pools, and command
  allocators that are reset only after the frame's fence has passed.

## Testing

- Linux clang ThreadSanitizer on the null backend for render-thread tests.
- Oversubscription tests for the job system (1, 2, 4, and more workers than
  cores).
- Resize and shutdown stress tests with frames in flight.
- A single-threaded reference mode compared against the threaded mode.

## What not to do

- Do not protect the whole renderer with one mutex. It hides races and destroys
  scaling.
- Do not let the render thread block on asset I/O; use placeholders and
  residency requests.
- Do not use `std::thread::detach` for engine work.
- Do not read game-thread containers from the render thread without a
  published snapshot or a command.
