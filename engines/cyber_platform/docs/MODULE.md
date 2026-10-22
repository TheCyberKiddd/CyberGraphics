# cyber_platform — module contract

**Kind:** platform
**Layer:** 1
**Order:** 10
**Status:** active
**Owner:** @TheCyberKiddd

## Purpose

Renderer-agnostic OS services: window creation, input, timing, file I/O, memory
mapping, file watching, native threads, and dynamic-library loading. `cyber_rhi`
and the renderer consume this module; this module never consumes them.

## Public API

`include/cyber/platform/`

| Header | Contents |
|---|---|
| `time.h` | `Clock`, `FrameTimer`, `FixedTimestep`. |
| `input.h` | `Key`, `MouseButton`, `InputState`. |
| `window.h` | `WindowDesc`, `WindowEvent`, `WindowEventType`, `Window`, `IsWindowSystemSupported`. |
| `filesystem.h` | `ReadFile`, `WriteFile`, `FileSize`, `Exists`, `IsDirectory`, `CreateDirectories`, `CurrentDirectory`, `ExecutableDirectory`, `MemoryMappedFile`, `FileWatcher`. |
| `thread.h` | `Thread`, `ThreadFunction`, `Mutex`, `ConditionVariable`, `MutexGuard`, `Atomic<T>`. |
| `library.h` | `DynamicLibrary`, typed symbol lookup. |
| `platform.h` | Umbrella include. |

Only headers under this path are public. Everything under `src/` is private.
Public headers are self-contained and never include `<windows.h>` or another
module's private headers. `NativeHandle()` returns an opaque `void*`.

### Failure and ownership

- All functions are non-throwing. Recoverable failures return `Result<T, Error>`
  or `Status<Error>`; programmer errors use assertions.
- `Window::Create` and `Thread::Start` report failure as values.
- `Window` and `MemoryMappedFile` are move-only and own their native resources.
- File reads return allocator-backed `DynamicArray<Byte>`; writes accept
  `Span<const Byte>`.
- `FileWatcher` uses its constructor allocator for watched paths and change
  reporting. Paths are UTF-8.
- `Profiler` pointers are non-owning.
- `DynamicLibrary` unloads on destruction.

## Dependencies

Allowed: cyber_core, cyber_memory

Forbidden: cyber_rhi, cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- No renderer or RHI includes, directly or transitively.
- Win32 headers and handles are isolated under `src/win32/`.
- Unsupported platforms provide a null window backend; they do not fail to
  compile.
- Window state is per-instance. No process-global mutable registry.
- The window event queue is fixed-capacity and never grows during a frame.
- Input state is updated by the window backend; callers only read it.
- Engine work never detaches a thread.
- Every runtime-owned buffer uses an explicit allocator.
- Paths are UTF-8. Invalid or missing paths return a stable `ErrorCode`.

## Non-goals

- Job scheduling (`cyber_jobs`).
- GPU device creation, swapchains, or present (`cyber_rhi`, Phase 6+).
- Audio, gamepads, clipboard, drag-and-drop, and monitor topology policy.
- OS directory-change notifications in the first pass.
- A non-null window backend on Linux.
- The stable C ABI; it remains reserved for Phase 25.

## Tests

`platform_time`: monotonic clock, frame timer, fixed timestep.
`platform_filesystem`: read/write, size, paths, directories, missing files,
empty files, memory mapping, and polling file watching.
`platform_thread`: start/join, naming, IDs, mutex contention, condition variables,
and move.
`platform_library`: invalid path and a known system-library symbol.
`platform_window`: unsupported-platform behavior; on Windows, hidden window
create, resize, fullscreen toggle, DPI, and event delivery.
`platform_input`: default state, per-frame edge clearing, and on Windows,
keyboard, mouse, wheel, and focus-loss transitions from real window messages.
`platform_public_headers`: every public header compiles together.

The `00_window` sample is the visible manual check. CI runs the automated
suite on Windows and compiles the full API on Ubuntu with the null backend.

## Budgets

- Window and input paths do not allocate per frame. The event queue is fixed at
  256 entries and input arrays are fixed at compile time.
- `Clock` reads the platform steady clock; no allocation.
- File and path functions are cold paths; they may allocate only through the
  supplied allocator.
- Thread creation cost and timer resolution are recorded on the target machine.
- Job scheduling is owned by `cyber_jobs`; this module only supplies timing and
  thread primitives.

## Roadmap

Phase 5.
