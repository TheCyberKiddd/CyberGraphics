# Threading model

This document describes the target model. It becomes real in Phase 5 (jobs)
and Phase 21 (render thread). Until then, the rules still apply to any code
that creates a thread.

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
3. **Frame data is immutable once published.** The game thread builds a frame
   packet, publishes it, and does not mutate it until the render thread signals
   completion.
4. **RHI objects belong to the render thread.** Creation may be deferred, but
   only the render thread records commands against them.
5. **Jobs declare dependencies.** A job never spins waiting for work that
   cannot run; it returns and lets the scheduler continue.
6. **Shutdown is a sequence, not a race.** Stop accepting work, drain the
   queue, wait for fences, destroy in reverse order.
7. **Resize and device-lost are normal paths.** They are tested, not treated as
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
