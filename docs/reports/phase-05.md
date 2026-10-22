# Phase 5 report: platform layer and job system

Date: 2026-10-22
Commits: `1a0708a..45e1f43` (platform, perf, and jobs implementation, tests,
sample, package integration, the formatting fix for CI, the window-event
assertions, and the scaling benchmark)
Machine: Windows 10 Pro 19045, Intel Core i5-6400 (4 cores, 4 threads), 16 GB
RAM, Intel HD 530, MSVC 19.44.35225, CMake 3.31.6, Ninja preset builds

## What was built

`cyber_platform`, `cyber_perf`, and `cyber_jobs` are active modules. The engine
can now open a window, read input, read and map files, start threads, load
libraries, measure time, and schedule parallel work without linking a renderer
or an RHI backend.

### Window and input

- `Window::Create` takes a `WindowDesc` and a caller-supplied `Allocator` and
  returns `Result<Window, Error>`; failure is a value, never an exception.
- Win32 backend under `src/win32/`: `GWLP_USERDATA` carries the implementation
  pointer, so there is no process-global window registry.
- Resize, fullscreen toggle/restore, DPI change, close, key, mouse-button, mouse
  move, raw mouse delta, and wheel events reach a fixed 256-entry event queue.
- `BeginFrame` clears per-frame input transitions and deltas, `PollEvents` pumps
  the OS queue, and `WaitEvents` blocks for one message.
- The null backend returns `ErrorCode::Unsupported`; `IsWindowSystemSupported()`
  lets hosts branch without platform macros.
- `InputState` tracks key and mouse-button edges, cursor position, wheel, and raw
  mouse motion.

### Time, files, threads, and libraries

- `Clock` reads the native high-resolution counter with `QueryPerformanceCounter`
  and `clock_gettime(CLOCK_MONOTONIC)`; no `<chrono>` in runtime code.
- `FrameTimer` returns non-negative deltas, clamps pathological frame spikes,
  and counts frames; `FixedTimestep` caps catch-up steps and exposes alpha.
- Files read into allocator-backed `DynamicArray<Byte>`; writes take
  `Span<const Byte>`; UTF-8 paths; missing files return `NotFound`.
- `MemoryMappedFile` is read-only and move-only, uses `CreateFileMappingW` /
  `MapViewOfFile` and `open`/`fstat`/`mmap`, and maps empty files to an empty
  span.
- `FileWatcher` polls timestamps and reports `Created`, `Modified`, and
  `Deleted`; `PollChange` drains the queue.
- `Thread::Start` uses `CreateThread`/`pthread_create` and returns
  `Status<Error>`; the start context is allocator-backed, carries a copied
  thread name, and every joinable thread is joined. Engine work never detaches.
- `Mutex`, `ConditionVariable`, `MutexGuard`, and `Atomic<T>` are thin
  standard-library wrappers with explicit ownership.
- `DynamicLibrary` wraps `LoadLibraryW`/`GetProcAddress` and `dlopen`/`dlsym`,
  with typed `GetFunction<T>` lookup.

### Job system

- Fixed worker pool sized from `Desc::workerCount` or the hardware count.
- Preallocated job records and a fixed slot table; a free-index stack recycles
  records without heap traffic or scanning.
- Per-worker bounded ring deques with back-pop by the owner and front-steal by
  other workers.
- Atomic dependency counters and `SubmitAfter`, including immediate submission
  when the dependency is already complete.
- `JobHandle` is a generation-checked `memory::Handle<JobTag>` that `Wait`
  consumes; stale handles return immediately.
- `SubmitDetached` is fire-and-forget and reports backpressure as `false`.
- `Wait` helps execute one ready job before blocking; `WaitIdle` observes
  completion without helping, so throughput tests reflect the worker count.
- `ParallelFor` batches a range over detached jobs with an atomic cursor and a
  stack-owned completion counter.
- Every job emits balanced `Profiler::BeginScope`/`EndScope` markers.
- `Shutdown` stops admission, drains queued work, joins every worker, and is
  idempotent.

### Profiler seam

`Profiler` exposes `BeginScope`, `EndScope`, and `Counter`; `NullProfiler` does
nothing and is available as `NullProfilerInstance`. The interface is small and
allocation-free so the job hot path can call it unconditionally.

### Sample and integration

- `samples/00_window` opens a 1280x720 window, prints resize/fullscreen/DPI
  events, toggles fullscreen on F11, closes on Escape, and runs no renderer.
- `CYBER_BUILD_SAMPLES` (default `ON`) wires the sample; on platforms without a
  window backend it prints a message and exits zero.
- `cyber::platform`, `cyber::perf`, and `cyber::jobs` install and export, and
  the package and source consumers link and run all three.
- The package config calls `find_dependency(Threads)` on POSIX.
- MSVC defines `_HAS_EXCEPTIONS=0` for every target; shipping LTO previously
  rejected mixed `std::system_error` layouts.

## Decisions

ADR [0007](../adr/0007-platform-and-job-scheduling.md), the
[Phase 5 design note](../design/phase-05-platform-jobs.md), and the module
contracts record the binding choices:

- `cyber_platform` depends only on `cyber_core` and `cyber_memory` and stays
  renderer-agnostic. Win32 code stays under `src/win32/`; other platforms get the
  null window backend.
- Window creation, thread start, file access, library loading, and job
  submission report failure as values. UTF-8 is the only path encoding.
- Threads use native creation; mutex, condition variable, and atomics are
  standard-library wrappers. Condition variables use untimed waits because the
  runtime builds without unwind semantics.
- The job system uses native threads, bounded per-worker rings, work stealing,
  dependency counters, generation handles, and deterministic shutdown. Job
  storage and queues are preallocated from the allocator given to `Create`.
- `cyber_perf` starts as a minimal profiler seam; real storage, GPU markers, and
  export belong to the performance phases.
- The stable plugin ABI remains reserved for Phase 25; integration is source
  mode or the installed CMake package.

## Evidence for done-when

The roadmap done-when line is: "a window opens with resize and fullscreen
events, and 10,000 jobs scale across all cores."

### Local presets

All 31 CTest entries pass in every local preset:

| Preset | Result |
|---|---|
| `debug` | 31/31 CTest entries passed |
| `release` | 31/31 CTest entries passed |
| `shipping` | 31/31 CTest entries passed |
| `debug-asan` | 31/31 CTest entries passed |

The Phase 5 suites are `platform_time`, `platform_filesystem`, `platform_window`,
`platform_input`, `platform_thread`, `platform_library`,
`platform_public_headers`, `perf_profiler`, `perf_public_headers`,
`jobs_job_system`, and `jobs_public_headers`.

Job coverage:

- `jobs_scaling_one_worker`, `jobs_scaling_two_workers`,
  `jobs_scaling_four_workers`, and `jobs_scaling_hardware_workers` each submit
  10,000 detached jobs and assert the exact completion count. On this 4-core
  machine the whole `jobs_job_system` entry, including creation, dependencies,
  `ParallelFor`, oversubscription, repeated shutdown, and profiler checks, runs
  in 0.47 s in `debug` and 0.18 s in `release`.
- `jobs_oversubscription_completes` runs more workers than hardware cores.
- `jobs_repeated_create_and_shutdown` creates and destroys the pool repeatedly
  and checks that no worker survives.
- `jobs_profiler_scopes_balance` proves the scope marker stream is balanced.
- `jobs_many_handles_and_generations` proves stale handle rejection.

Scaling is measured separately by the developer-only `cyber_jobs_bench` target
(`engines/cyber_jobs/benchmarks/job_scaling_bench.cpp`, built with
`CYBER_BUILD_TESTS`). It submits 10,000 detached jobs with 50,000 hash
iterations each and reports the best of three runs in `release`:

| Worker count | Wall time | Speedup | Jobs/second |
|---|---|---|---|
| 1 | 1104.13 ms | 1.00x | 9,057 |
| 2 | 562.40 ms | 1.96x | 17,781 |
| 4 | 309.29 ms | 3.57x | 32,332 |
| 4 (hardware count) | 305.04 ms | 3.62x | 32,783 |

```powershell
.\scripts\build.ps1 -Preset release
.\build\release\engines\cyber_jobs\cyber_jobs_bench.exe
```

The work is CPU-bound integer mixing per job, so the near-linear 1-to-2 worker
result and 3.6x on four cores show the pool and its queues scale rather than
serializing on a global lock.

Window and input coverage:

- `platform_window_create_and_close` creates a hidden Win32 window, renames and
  resizes it, asserts a non-empty resize event, enters fullscreen and asserts the
  `FullscreenChanged` on event plus the state, restores windowed mode and asserts
  the off event, then moves the window, closes it, and asserts the close event.
- On platforms without a window backend, `platform_window_create_and_close`
  asserts `Unsupported` instead.
- `platform_input_from_window_messages` drives input through real window
  messages and checks key/button edges, cursor movement, and wheel deltas.
- The visible-window path is the sample. It was launched on this machine, was
  still running with a visible window after three seconds, and was then
  terminated by the smoke check. The automated equivalent is the hidden-window
  test above.

Commands:

```powershell
.\scripts\build.ps1 -Preset debug
.\scripts\test.ps1 -Preset debug
.\scripts\test.ps1 -Preset release
.\scripts\test.ps1 -Preset shipping
.\scripts\test.ps1 -Preset debug-asan
```

### Package and source consumers

```text
scripts\verify_package.ps1 -Preset debug
install + package consumer configure, build, run: exit 0

scripts\verify_package.ps1 -Preset release
install + package consumer configure, build, run: exit 0

build\source-consumer\source_consumer.exe
exit 0
```

`verify_package.ps1` now requires `cyber/platform/platform.h`,
`cyber/platform/window.h`, `cyber/platform/thread.h`, `cyber/perf/perf.h`,
`cyber/perf/profiler.h`, `cyber/jobs/jobs.h`, and `cyber/jobs/job_system.h` in
the installed prefix. The consumers read the clock, emit profiler markers, and
submit a detached job before `WaitIdle` and `Shutdown`, through the installed
package and through `add_subdirectory`.

### Repository checks

```text
python scripts/check_structure.py
Structure check passed: 16 modules, 1 contract(s).

python scripts/layer_check.py
Layer check passed: 16 modules.

python scripts/check_format.py
Formatting check passed: 139 files.
```

### CI

GitHub Actions run [39774150763](https://github.com/TheCyberKiddd/CyberGraphics/actions/runs/39774150763)
passed for `45e1f43`:

| Job | Result | Time |
|---|---|---|
| Structure and dependency law | pass | 5 s |
| Formatting | pass | 14 s |
| Windows MSVC (configure, build, test, install, consumers) | pass | 1 m 15 s |
| Linux clang + UBSan (configure, build, test, package, consumers) | pass | 35 s |

The first run for `e374b60` failed only the formatting job: clang-format 18 on
Ubuntu inserts a brace break for `struct stat status{};` and
`struct timespec time{};`, while local clang-format 19 leaves them inline. The
fix uses `= {}` in both POSIX sources and was verified locally with
clang-format 18.1.8 and 19.1.5. The run above includes that fix, the strengthened
window test, and the job-scaling benchmark, and passes all jobs.

## Known gaps

- The Linux window backend is the null backend. Windows is the only tested
  window backend; CI compiles and links the full public API on Linux.
- `FileWatcher` polls timestamps. Two writes inside one filesystem timestamp tick
  can be missed; it is a hot-reload convenience, not an audit log.
- Fullscreen, DPI, and raw input behavior varies with monitor topology and
  remote sessions. The automated window test is hidden; visual confirmation is
  the sample.
- The event queue drops the oldest event when a producer overruns 256 entries.
- `SubmitDetached` reports queue backpressure as `false`; callers must handle
  rejection when the bounded queues are full.
- A job handle that is never waited or released keeps its record alive until
  shutdown.
- Nested `ParallelFor` from a worker, job cancellation, priorities, fibers, and
  GPU scheduling are not supported in this phase.
- `cyber_perf` has no storage, aggregation, visualization, or export yet.
- TSan is named as Linux hardening in the job-system contract but is not part
  of the current CI matrix; correctness evidence is the stress, oversubscription,
  lifecycle, and sanitizer runs above.
- Local clang-format 19.1.5 and CI clang-format 18 now both pass, but CI
  formatting remains authoritative.
- Branch protection is still a GitHub repository setting and is not encoded in
  the repository.
- The stable C ABI remains empty until Phase 25; `cyber_platform`, `cyber_perf`,
  and `cyber_jobs` are C++ module interfaces, not a shipping ABI.

## Sources used

- Microsoft, "Get Started with Win32 and C++", "Raw Input Overview",
  "Acquiring high-resolution time stamps", and the `CreateThread`,
  `SetThreadDescription`, `LoadLibrary`, `CreateFileMapping`, and `MapViewOfFile`
  reference pages.
- POSIX `pthread_create`, `pthread_setname_np`, `clock_gettime`, `mmap`, and
  `dlopen` reference pages.
- Glenn Fiedler, "Fix Your Timestep!".
- Naughty Dog, "Parallelizing the Naughty Dog Engine Using Fibers" (job and
  frame-work patterns; fibers are not used in this phase).
- C++20 standard library: `std::filesystem`, `std::mutex`,
  `std::condition_variable_any`, `std::atomic`.
- CyberGraphics ADR 0007, the Phase 5 design note,
  `docs/architecture/threading.md`, and the module contracts.

## What the next phase needs from this one

Phase 6 can rely on:

- `Window` with `NativeHandle()`, client size, resize events, and a frame loop
  (`BeginFrame`, `PollEvents`, `WaitEvents`).
- `FrameTimer` and `FixedTimestep` for frame pacing and the frame-time readout.
- `IsWindowSystemSupported()` to keep headless CI builds and tests honest.
- Allocator-backed file reads and `MemoryMappedFile` for loading test assets and
  shaders.
- `JobSystem::ParallelFor` for tiled work, plus `WaitIdle` and deterministic
  shutdown for frame boundaries and teardown.
- `Profiler` scope markers around present and frame phases.
- Installed-package and source-mode integration for the products that Phase 6
  will add.

The platform layer must not gain renderer includes when Phase 6 presents a
framebuffer: presenting goes through the platform window API, and device and
swapchain ownership stays in the renderer and RHI layers.
