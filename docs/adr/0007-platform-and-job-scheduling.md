# ADR 0007: Platform boundary and job scheduling

Date: 2026-10-21
Status: accepted

## Context

CyberGraphics is consumed by Cyber Engine as a renderer submodule or installed
CMake package. The platform layer must therefore stay independent of the
renderer and the RHI: it owns the window, input, time, files, threads, and
dynamic libraries, but it never owns GPU devices or draw submission. Windows is
the shipping target, while Ubuntu clang is a required CI platform.

Phase 5 also introduces the first concurrency service. The job system must make
dependencies and shutdown explicit, scale across cores, and remain usable by
the software renderer and later by the RHI. The runtime has exceptions and RTTI
disabled, so allocation and thread creation failures must be reported as values.

## Decision

1. `cyber_platform` exposes renderer-agnostic C++ types only. Win32 headers and
   handles stay under `src/win32/`; the public API uses opaque native handles.
   Unsupported platforms provide a null window backend and return
   `ErrorCode::Unsupported` instead of failing to compile.
2. `cyber_platform` depends on `cyber_core` and `cyber_memory`. File reads,
   paths, event payloads, and watcher state use explicit allocators rather than
   hidden standard-library allocation. It never includes `cyber_rhi`,
   `cyber_renderer`, or `cyber_graphics`.
3. Windows are created with a caller-supplied allocator. Each window owns a
   fixed-size event queue and input state; mouse movement uses raw input, while
   resize, DPI, focus, keyboard, text, and button events are normal window
   messages. A window owns no process-global mutable state.
4. Paths are UTF-8. File operations return `Result`/`Status` values, reads return
   allocator-backed `DynamicArray<Byte>`, and memory-mapped files expose a
   read-only `Span`. The first file watcher is polling-based; OS change
   notifications can replace it without changing the public API.
5. Threads use native Windows/POSIX creation so failure is a `Status` and thread
   naming is explicit. Mutexes, condition variables, and atomics are thin
   standard-library wrappers. Engine work never uses detached threads.
6. `cyber_jobs` uses a fixed worker pool with per-worker ring deques and
   work stealing. Jobs carry a dependency counter. A dependency list is
   protected by one scheduler mutex; completion queues ready dependents.
   Handles carry an index and a generation, and `Wait` helps execute ready work
   instead of spinning. Shutdown stops admission, drains queued work, and joins
   every worker.
7. `cyber_perf` starts with a small `Profiler` interface and a null profiler.
   The job system emits begin/end scope markers around every job. Frame stats,
   counters, GPU markers, and export remain later work.
8. The stable plugin ABI and renderer products remain Phase 25 work. Until then,
   integration is source-mode `add_subdirectory` or the installed CMake package;
   no renderer header is allowed in the platform layer.

## Consequences

- Cyber Engine can embed the platform layer without pulling in a renderer or a
  graphics API. A different renderer can reuse the same window and job services.
- The null window backend keeps Linux CI meaningful for files, threads, jobs,
  and package integration even though the shipping window backend is Win32.
- Allocator-backed file and watcher APIs make ownership visible and let tools
  use arenas or tracking allocators.
- Polling file watching costs one stat per watched file per poll. It is adequate
  for asset hot reload in the first pass and is replaceable behind the same API.
- Native thread creation is more code than `std::thread`, but it gives explicit
  failure reporting, deterministic join, and thread names without hidden
  exceptions.
- The job system is not a fiber scheduler. A job must not block on unrelated work
  that cannot progress; nested waits are a documented hazard until a fiber
  scheduler exists.
- The minimal profiler interface has no storage or visualization yet. The cost
  when disabled is one null virtual call per job in the first pass.

## Alternatives

- Put window creation in the RHI: couples the renderer to a graphics API and
  prevents a headless or alternate renderer from reusing input and timing.
- Use `std::thread` directly everywhere: portable, but construction failure
  throws and thread naming remains platform-specific.
- Use standard-library `std::deque` per worker: simple, but every submission
  allocates and the allocation path is outside the project allocator.
- Use fibers immediately: higher throughput potential, but much higher
  complexity and not justified before measurements.
- Use OS directory-change notifications in Phase 5: lower latency, but a larger
  platform-specific surface before the asset pipeline exists.

## Follow-up

Phase 6 presents a framebuffer through the platform window. Phase 7 uses the job
system for tiled rasterization. Phase 10 and later consume the platform thread
and file services from the RHI and asset pipeline. Phase 15/36 extend
`cyber_perf` with real frame capture, counters, GPU markers, and export.
