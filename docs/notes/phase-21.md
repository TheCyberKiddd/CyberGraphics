# Phase 21 reading notes

Date: 2026-11-06

## Sources

- Christian Gyrling, “Parallelizing the Naughty Dog Engine Using Fibers,”
  Advances in Real-Time Rendering in Games, 2015. The linked PDF endpoint in
  the build guide returned HTTP 404 during this phase; no undocumented details
  from the talk are claimed.
- Microsoft, [Work submission in Direct3D 12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/command-queues-and-command-lists),
  accessed 2026-11-06.
- CyberGraphics `cyber_platform` thread, `cyber_perf` profiler, RHI command
  submission, and `FrameRing` contracts.

## What applies to CyberGraphics

- Publish immutable frame snapshots across the game/render ownership boundary;
  do not hand the renderer mutable game-thread storage.
- Bound both queued commands and copied packet storage, and make saturation
  observable rather than blocking the producer.
- Keep CPU packet reuse separate from GPU resource reuse; GPU lifetimes still
  require RHI fences.
- Preserve producer order for resize and frame commands, and drain accepted
  work before render-owned teardown.
- Keep a single-threaded mode that follows the same callback and packet rules.

## What does not apply, and why

No engine source or implementation details are copied. The phase uses
CyberGraphics' own SPSC queue, platform thread, RHI, profiler, allocator, and
error contracts. The inaccessible talk content is not treated as a specification.

## Open questions

Parallel command-list recording, multiple render consumers, and cross-queue
scheduling remain outside this phase; they require separate RHI ownership and
synchronization decisions.
