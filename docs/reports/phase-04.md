# Phase 4 report: memory and containers

Date: 2026-10-21
Commits: `a5198d3..b481e12` (memory implementation, package integration,
documentation, and the UBSan/RTTI build fixes)
Machine: Windows 10 Pro 19045, Intel Core i5-6400, 16 GB RAM, Intel HD 530,
MSVC 19.44.35225, CMake 3.31.6, Ninja preset builds

## What was built

`cyber_memory` is now the third active foundation module. It provides one
non-throwing allocation boundary, scoped allocators, diagnostics, containers,
strings, and generation-counted handles.

### Allocators

- `Allocator`: explicit alignment, `Deallocate`, `Reset`, `Owns`, and
  `AllocatorStats`. Zero-size allocation returns `nullptr`; invalid alignment
  returns `nullptr`; allocation never throws.
- `HeapAllocator`: root allocator over `std::malloc`/`std::free`, with a 16-byte
  header, alignment padding, and live/peak/accounting statistics.
- `LinearAllocator`: bump allocation with `Reset`, `Remaining`, and ownership
  queries.
- `StackAllocator`: LIFO markers and `FreeTo`, rejecting out-of-order or
  out-of-range markers.
- `PoolAllocator`: fixed-block allocation, intrusive free list, reuse, block
  exhaustion, external or owned storage, and reset.
- `FreeListAllocator`: variable-size blocks with 64-byte boundary-tag headers,
  first-fit placement, split/merge, address-order coalescing, alignment,
  `IsValid`, free-block count, and largest-free-block reporting.
- `RingAllocator`: transient wraparound allocation, reset, live-byte refusal,
  and documented pointer invalidation after a wrap.
- `FrameAllocator`: `BeginFrame`/`EndFrame` over linear storage.
- `TrackingAllocator`: tagged allocations, intrusive 64-byte headers,
  allocation-free leak reporting, and leak capture without touching the
  upstream allocator's lifetime.
- `AllocatorStats`: capacity, payload used, peak payload used, and successful
  allocation count. Overhead is documented per allocator in the module contract.

### Containers and handles

- `Span<T>`: non-owning, const conversion, subviews, first/last, and bounds
  assertions.
- `StringView`: non-owning UTF-8 bytes, comparison, prefix/suffix, find,
  deterministic xxHash64, and null-safe empty handling.
- `String`: owned null-terminated storage, append/assign/reserve, self-aliasing
  detection, allocator-aware move, capacity growth, and overflow refusal.
- `DynamicArray<T>`: allocator-aware growth, non-trivial construction and
  destruction, move transfer, failed-growth preservation, and overflow refusal.
- `FixedArray<T, N>`: inline storage with `PushBack`/`PopBack`/`Clear`.
- `HashMap<K, V>`: open addressing, linear probing, tombstones, power-of-two
  capacity, ~70% load growth, rehash cleanup, insert-or-assign, remove, clear,
  and move.
- `Handle<Tag>`: 64-bit index plus generation.
- `SlotMap<T, Tag>`: dense slot storage, recycled indices, generation bump on
  remove/clear, stale-handle rejection, and slot reuse.

## Decisions

ADR [0006](../adr/0006-memory-allocation-and-handles.md) and the
[Phase 4 design note](../design/phase-04-memory.md) record the binding choices:

- One explicit, non-throwing `Allocator` interface with power-of-two alignment,
  ownership queries, and accounting. Allocation failure is `nullptr`; recoverable
  container failures return `bool` or a pointer.
- Bump allocators reclaim through `Reset` or stack markers. Variable-size reuse
  uses 64-byte boundary tags, first-fit placement, and address-order coalescing.
- Leak diagnostics use intrusive 64-byte tracking headers and a callback path
  that performs no allocation.
- Containers take an allocator by reference, are non-copyable when ownership is
  ambiguous, and transfer ownership on move. Growth doubles capacity, rejects
  overflow, and leaves the old allocation intact on failure.
- `HashMap` uses open addressing, linear probing, tombstones, power-of-two
  capacity, and ~70% load. `SlotMap` recycles slots and validates index plus
  generation on every lookup.
- No global mutable state, no exceptions, no RTTI, and no hidden allocation in
  runtime paths. Thread-local and concurrent allocation are Phase 5 work.

## Evidence for done-when

### Local presets

All tests pass in every local preset:

| Preset | Result |
|---|---|
| `debug` | 20/20 CTest entries passed |
| `release` | 20/20 CTest entries passed |
| `shipping` | 20/20 CTest entries passed |
| `debug-asan` | 20/20 CTest entries passed |

The memory suite adds `memory_allocator`, `memory_container`, and
`memory_public_headers` to the existing core and math tests. The allocator suite
includes a deterministic 128-block randomized allocate/free stress run against
`FreeListAllocator`, validating the heap after every operation and proving full
coalescing at the end. The deliberate-leak case proves the tracking report sees
exactly one outstanding allocation and its tag.

Commands:

```powershell
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

build\source-consumer-local\source_consumer.exe
exit 0
```

`verify_package.ps1` now requires `cyber/memory/memory.h`,
`cyber/memory/allocator.h`, `cyber/memory/dynamic_array.h`, and
`cyber/memory/string_view.h` in the installed prefix. The package consumer
exercises allocation, a dynamic array, a string-keyed hash map, and a slot map
through the installed CMake package. The source consumer exercises the same
code through `add_subdirectory`.

### Repository checks

```text
python scripts/check_structure.py
Structure check passed: 16 modules, 1 contract(s).

python scripts/layer_check.py
Layer check passed: 16 modules.

.\scripts\format.ps1 -Check
exit 0
```

### CI

GitHub Actions run [39584029300](https://github.com/TheCyberKiddd/CyberGraphics/actions/runs/39584029300)
passed for `b481e12`:

| Job | Result | Time |
|---|---|---|
| Structure and dependency law | pass | 4 s |
| Formatting | pass | 16 s |
| Windows MSVC (configure, build, test, install, consumers) | pass | 35 s |
| Linux clang + UBSan (configure, build, test, package, consumers) | pass | 1 m 12 s |

The first Linux run exposed two build-contract defects: UBSan's `vptr` check
requires RTTI, which runtime code disables, and test targets were compiled with
RTTI while the polymorphic allocator libraries were not. The fix disables the
`vptr` check only for UBSan and compiles tests with the same RTTI setting as the
runtime while keeping exceptions enabled for the test helpers. The final run
passes all jobs. The only annotation is the upstream
`ilammy/msvc-dev-cmd` Node 20 deprecation notice; it does not affect the build
or tests.

## Known gaps

- UBSan excludes its `vptr` check because runtime code and tests are built with
  RTTI disabled. Arithmetic, bounds, alignment, and the other UBSan checks still
  run.
- `FreeListAllocator` and `TrackingAllocator` cap payload alignment at 64 bytes.
  Over-aligned types need a dedicated allocator.
- `RingAllocator` may reuse the oldest bytes after wraparound; pointers from
  before a wrap are invalid by contract.
- `HashMap` requires default-constructible, move-assignable keys and values.
  `StringView` keys are non-owning and must outlive the map entry.
- `SlotMap` requires a default-constructible, move-assignable `T`. Generation
  wraparound is theoretical but not separately tested.
- Allocators and containers are single-threaded. Synchronization and
  thread-local arenas belong to Phase 5.
- The package consumer is a build/link/runtime smoke test, not a sanitizer run.
  Sanitizer instrumentation stays build-local.
- Local `clang-format` is 19.1.5 while Ubuntu CI uses 18.x. Both accept the tree;
  CI formatting is authoritative.
- Branch protection is still a GitHub repository setting and is not encoded in
  the repository.
- The stable C ABI remains empty until Phase 25; `cyber_memory` is a C++ module
  interface, not a shipping ABI.

## Sources used

- Naughty Dog, "Parallelizing the Naughty Dog Engine Using Fibers" (frame
  allocation and explicit ownership patterns).
- ISO C++20: `std::construct_at`, `std::destroy_at`, `std::malloc`,
  `std::free`, `std::memmove`, `std::launder` constraints.
- Intel and Microsoft alignment guidance for power-of-two allocators.
- Open-addressing hash-table notes on linear probing, tombstones, and load
  factor.
- Generation-counted handle practice from entity/component systems and GPU
  resource tables.
- CyberGraphics ADR 0006, the Phase 4 design note, and the module contract.

## What the next phase needs from this one

Phase 5 can rely on:

- `cyber::memory` as an installed C++ module target with self-contained public
  headers and source-mode support.
- `Allocator`, `HeapAllocator`, `TrackingAllocator`, and `FrameAllocator` for
  platform services, job scratch memory, and per-frame work.
- `DynamicArray`, `HashMap`, `String`, and `StringView` for platform and job
  bookkeeping without hidden global allocation.
- `Handle<Tag>` and `SlotMap<T, Tag>` for generation-checked platform and job
  objects.
- Explicit allocator ownership, move-only containers, and the no-throw failure
  convention that the rest of the engine will follow.

Phase 5 should add `cyber_platform` and `cyber_jobs` without changing the
memory public interface; if it needs thread-local or concurrent allocation, that
is a new allocator type and a new contract, not a silent change to the existing
ones.
