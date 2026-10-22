# Phase 5 — Platform layer and job system

Date: 2026-10-21
Status: implemented
Related: ADR 0007, `cyber_platform` contract, `cyber_jobs` contract,
[threading model](../architecture/threading.md)

## What this phase builds

Phase 5 makes the engine run on a real OS surface and gives it a scheduler:

- `cyber_platform`: window creation, resize, DPI, fullscreen, fixed-size event
  queue, keyboard/mouse state with raw mouse deltas, high-resolution clocks, a
  frame timer, a fixed-timestep helper, UTF-8 file I/O, memory-mapped files,
  path helpers, a polling file watcher, native thread wrappers, mutex/condition
  variable/atomic wrappers, thread naming, and dynamic-library loading.
- `cyber_jobs`: a fixed worker pool, per-worker ring deques, work stealing,
  dependency counters, job handles, `ParallelFor`, deterministic shutdown, and
  profiler scope markers.
- `cyber_perf`: a minimal profiler interface and a null implementation. It is
  the seam for later frame stats, counters, GPU markers, and export.
- A small visible-window sample that proves the platform layer can be run
  without the renderer.

Windows is the shipping window backend. Linux compiles the same public API with
a null window backend and exercises files, threads, jobs, and package
integration in CI.

## What it deliberately leaves out

- RHI device creation, swapchains, and present. Those belong to Phase 10+ and
  Phase 6 respectively.
- The stable plugin ABI in `include/cybergraphics/`. It stays empty until
  Phase 25.
- A renderer or any renderer include. `cyber_platform` must remain reusable by
  an alternate renderer.
- OS directory-change notifications. The first watcher polls timestamps.
- Fiber scheduling, job cancellation, priorities, and GPU scheduling.
- Full profiler storage, aggregation, visualization, and export.
- Audio, gamepads, clipboard, drag-and-drop, and multiple monitor policy.
- A cross-platform window backend other than the null fallback.

## Interfaces it touches

New public targets: `cyber::platform`, `cyber::jobs`, and `cyber::perf`.

### `cyber_platform` public headers

| Header | Responsibility |
|---|---|
| `time.h` | `Clock`, `FrameTimer`, `FixedTimestep`. |
| `input.h` | `Key`, `MouseButton`, `InputState`. |
| `window.h` | `WindowDesc`, `WindowEvent`, `Window`, window-system query. |
| `filesystem.h` | File read/write, path queries, `MemoryMappedFile`, `FileWatcher`. |
| `thread.h` | `Thread`, `Mutex`, `ConditionVariable`, `MutexGuard`, `Atomic<T>`. |
| `library.h` | `DynamicLibrary` and typed symbol lookup. |
| `platform.h` | Umbrella include. |

All public functions are non-throwing. Recoverable failures use
`Result<T, Error>` or `Status<Error>`. Window creation and thread start report
failure instead of throwing. File paths are UTF-8 and do not cross the C ABI.

### `cyber_jobs` public headers

| Header | Responsibility |
|---|---|
| `job_system.h` | `JobSystem`, `JobDesc`, `JobHandle`, `JobFunction`, `ParallelFunction`. |
| `jobs.h` | Umbrella include. |

`JobHandle` is a generation-counted `cyber::memory::Handle<JobTag>`. A job handle
is valid until `Wait` consumes it. Fire-and-forget work uses `SubmitDetached`.

### `cyber_perf` public headers

| Header | Responsibility |
|---|---|
| `profiler.h` | `Profiler`, `NullProfiler`, `NullProfilerInstance`. |
| `perf.h` | Umbrella include. |

## Design

### Window and input

`Window` owns a private implementation allocated from a caller-provided
`Allocator`. Public headers never include `<windows.h>`; `NativeHandle()` returns
an opaque `void*` for a host that needs the HWND.

The event queue is fixed at 256 entries. If a producer overruns it, the oldest
event is dropped; the queue is a frame-boundary diagnostic, not an unbounded
log. `BeginFrame` clears per-frame input transitions and mouse/wheel deltas.
`PollEvents` pumps the OS queue; `WaitEvents` blocks for one message.

Win32 code lives under `src/win32/`. The window procedure stores its
`WindowImpl*` in `GWLP_USERDATA`, so no process-global registry is needed. Raw
input supplies relative mouse motion; legacy mouse messages supply cursor
position and button edges. Fullscreen stores the windowed style and rectangle,
then restores both on exit. DPI changes resize the window to the suggested
rectangle and emit a DPI event.

The null backend returns `ErrorCode::Unsupported` from `Window::Create`.
`IsWindowSystemSupported()` lets tests and tools branch without platform macros.

### Time

`Clock` uses the native high-resolution counter: `QueryPerformanceCounter` on
Windows and `clock_gettime(CLOCK_MONOTONIC)` on POSIX. The runtime builds with
unwind semantics disabled (`/EHs-c-`), so `<chrono>` is avoided and condition
variables use untimed waits. On MSVC every target, including tests and tools,
compiles the standard library with `_HAS_EXCEPTIONS=0`; mixing settings gives
`std::system_error` different layouts and breaks shipping LTO. Test executables
still use `/EHsc` so the harness can catch unexpected exceptions.
`FrameTimer` returns elapsed seconds, clamps
pathological frame spikes, and counts frames. `FixedTimestep` accumulates frame
time, caps the number of catch-up steps, and exposes the interpolation alpha. No
time API allocates.

### Files

File reads return allocator-backed `DynamicArray<Byte>`. Writes accept
`Span<const Byte>`. `std::filesystem` and `std::ifstream` are used only as
cold-path OS access; buffers and path results use project allocators.

`MemoryMappedFile` uses `CreateFileMappingW`/`MapViewOfFile` on Windows and
`open`/`fstat`/`mmap` on POSIX. It exposes a read-only `Span` and is move-only.
Zero-length files map to an empty span.

`FileWatcher` stores watched paths and the last observed timestamp. `Poll`
compares timestamps and queues `Created`, `Modified`, or `Deleted` changes;
`PollChange` drains them. It is deliberately polling-based in the first pass.

### Threads and libraries

`Thread` uses `CreateThread` on Windows and `pthread_create` on POSIX. The start
context is allocated from an explicit allocator and contains a copied thread
name. `Start` returns `Status<Error>`; `Join` is deterministic; the destructor
joins a joinable thread. `Mutex`, `ConditionVariable`, and `Atomic<T>` are thin
wrappers over the standard library. Engine work never detaches.

`DynamicLibrary` wraps `LoadLibraryW`/`GetProcAddress` and
`dlopen`/`dlsym`. Symbol lookup returns `void*`; `GetFunction<T>` performs the
typed cast. The library is unloaded on destruction.

### Job system

The scheduler preallocates job records and a fixed slot table for handles at
creation. A free-index stack recycles records in constant time; allocation and
free never touch the heap and never scan a free list. Each worker owns a bounded
ring deque of `Job*`; a worker pops from its own back and steals from another
worker's front. The queues are bounded by the job-slot capacity, and submission
fails cleanly if no queue has room.

Work wakeups and completion wakeups use separate condition variables and
mutexes. Worker and waiter counts let a notifier skip the lock when nobody is
blocked, and a notifier always holds the associated mutex while signalling, so
a waiter cannot check its predicate and then miss the wakeup.

A job has an atomic dependency count. `SubmitAfter` either enqueues immediately
when the dependency is already complete or links the new job into the
dependency's list under the scheduler mutex. Completion marks the job complete,
moves the dependent list out, decrements each dependent, and enqueues those that
reach zero. `Wait` observes completion, helps execute one ready job before
blocking, then releases the handle. `WaitIdle` waits for all active work without
helping, so throughput measurements reflect the configured worker count.

Shutdown stops admission, marks the pool stopping, wakes workers, lets queued
work drain, and joins every worker. A job must not wait on unrelated work that
cannot progress; nested `ParallelFor` from a worker is not supported in the
first pass.

`ParallelFor` splits the range into batches and uses detached jobs, so it does
not consume handle slots. Each batch pulls the next index range from an atomic
cursor. The completion counter is stack-owned and the call waits until every
batch finishes.

### Profiling seam

`Profiler` has `BeginScope`, `EndScope`, and `Counter`. The null implementation
does nothing. The job system calls `BeginScope(job.name)` and `EndScope` around
every job. A counting profiler in tests proves balanced markers. Real storage
and export arrive with the performance phase; the interface is deliberately
small so the job hot path does not allocate.

### Integration

`cyber_platform` depends on `cyber_core` and `cyber_memory`.
`cyber_perf` depends on `cyber_core` and `cyber_platform`.
`cyber_jobs` depends on `cyber_core`, `cyber_memory`, `cyber_platform`, and
`cyber_perf`. The package exports `Threads::Threads` and the platform dynamic
library dependency on POSIX. The package and source consumers link all three
new targets.

The renderer remains a separate layer. This is the pluggability boundary: a host
can create a window and schedule work without linking a renderer, and the RHI
will later receive the platform window handle through its own public API.

## Alternatives considered

- **`std::thread` and `std::deque` everywhere.** Less code, but thread creation
  failure would terminate and job submission would allocate outside the project
  allocator.
- **A single global queue.** Simpler, but does not meet the work-stealing
  requirement and creates a contention point for tiled rasterization.
- **Fibers in Phase 5.** Better for blocking jobs, but not justified before the
  renderer has measured a need.
- **OS file-change notifications.** Better latency, but a larger Win32 surface
  before the asset hot-reload path exists.
- **Renderer-owned window.** Simpler for one renderer, but prevents Cyber Engine
  and alternate renderers from sharing platform services.

## Risks

- Win32 fullscreen and DPI behavior varies with monitor topology and remote
  desktop sessions. The automated test uses a hidden window; the sample is the
  manual visible-window check.
- Raw input can be unavailable in remote sessions. Mouse deltas remain zero
  while legacy cursor events still work.
- A bounded event queue can drop events under extreme message floods. It drops
  the oldest event and keeps the newest state, which is the useful trade-off for
  a game loop.
- Job handles require the caller to wait or release them. A forgotten handle
  keeps a job record alive until shutdown; the contract documents this.
- Bounded worker queues can reject work. `SubmitDetached` returns false and the
  caller must handle backpressure; the 10,000-job test stays below capacity.
- Polling file watching can miss two writes within one filesystem timestamp
  tick. It is a hot-reload convenience, not an audit log.
- Linux has no window backend, so visual behavior is tested on Windows only.
  CI still compiles and links the full public API.

## Test plan

`platform_time`:

- monotonic clock advances;
- frame timer delta and total are non-negative and frames count;
- fixed timestep yields the expected number of steps and alpha.

`platform_filesystem`:

- write/read round-trip, file size, exists, directory creation;
- missing file returns `NotFound`;
- empty file reads as an empty array;
- memory-mapped file bytes match the source and move transfers ownership;
- watcher sees a modified file and reports the watched path.

`platform_thread`:

- thread start, naming, join, and current-thread ID;
- mutex protects a shared counter under contention;
- condition variable wakes a waiter;
- moved thread remains joinable.

`platform_library`:

- invalid library returns an error;
- loading a system library and resolving a known symbol works on the host.

`platform_window`:

- unsupported platforms return `Unsupported`;
- Windows creates a hidden window, resizes it, receives resize/fullscreen
  events, and reports the expected DPI and size.

`jobs`:

- 10,000 detached jobs complete with the expected count on 1, 2, 4, and
  hardware-worker configurations;
- dependency chains run in order;
- `ParallelFor` covers every index exactly once;
- oversubscription (more workers than hardware) completes;
- repeated create/shutdown cycles leave no live workers;
- profiler scopes balance.

`platform_public_headers`, `jobs_public_headers`, and `perf_public_headers`
compile every public header together.

## Sources

- Microsoft, "Get Started with Win32 and C++".
- Microsoft, "Raw Input Overview" and `RAWINPUT`/`RAWMOUSE` reference.
- Microsoft, "Acquiring high-resolution time stamps".
- Microsoft, `CreateThread`, `SetThreadDescription`, `LoadLibrary`,
  `CreateFileMapping`, and `MapViewOfFile` reference.
- POSIX `pthread_create`, `pthread_setname_np`, `mmap`, `dlopen` reference.
- Glenn Fiedler, "Fix Your Timestep!".
- Naughty Dog, "Parallelizing the Naughty Dog Engine Using Fibers" (job and
  frame-work patterns; no fibers in this phase).
- C++20 standard library: `std::filesystem`, `std::mutex`,
  `std::condition_variable_any`, `std::atomic`; native `QueryPerformanceCounter`
  and `clock_gettime` for time because `<chrono>` is avoided under
  `/EHs-c-`.
- CyberGraphics ADR 0007, `docs/architecture/threading.md`, and the module
  contracts.
